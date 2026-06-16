#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace maritime {

// ---------------------------------------------------------------------------
// Weather grid constants — two distinct grids, confirmed against the real
// daily-average dataset (see average_weather_description.md):
//
//   Wave grid: sigwh, wsh, wsp, wsd, pwd, swell_residual
//     shape:  (621, 1440)   lat -75°→+80° (row 0 = south), lon 0→359.75E
//   Wind grid: was, wad
//     shape:  (721, 1440)   lat -90°→+90° (row 0 = south), lon 0→359.75E
//
//   Both grids: 0.25° resolution, row 0 = SOUTH (south-first).
//   dtype:  float16 (_Float16 / uint16_t storage) once loaded into the buffer
//   NaN:    land mask — redirected at lookup time via the per-grid snap table
// ---------------------------------------------------------------------------
inline constexpr int WX_NI          = 1440;
inline constexpr float WX_RES       = 0.25f;

inline constexpr int WAVE_NJ        = 621;
inline constexpr float WAVE_LAT_MIN = -75.f;
inline constexpr int WAVE_N_POINTS  = WAVE_NJ * WX_NI;   // 894,240

inline constexpr int WIND_NJ        = 721;
inline constexpr float WIND_LAT_MIN = -90.f;
inline constexpr int WIND_N_POINTS  = WIND_NJ * WX_NI;   // 1,038,240

inline constexpr int WX_N_TIMESTEPS = 24;                // hourly, 24h horizon
inline constexpr int WAVE_N_TOTAL   = WX_N_TIMESTEPS * WAVE_N_POINTS;
inline constexpr int WIND_N_TOTAL   = WX_N_TIMESTEPS * WIND_N_POINTS;

// ---------------------------------------------------------------------------
// WeatherBuffer
//
// Rule of Zero: all members are std::vector, which owns and manages memory
// via RAII.  The compiler-generated destructor frees all allocations.
// No user-defined lifecycle functions.
//
// Structure of Arrays (SoA) layout: wave variables occupy a vector of
// WAVE_N_TOTAL float16 values, wind variables a vector of WIND_N_TOTAL,
// each indexed as:
//
//     [time_step * <grid>_N_POINTS + lat_i * WX_NI + lon_i]
//
// float16 is kept native throughout; callers cast to float32 at the
// arithmetic site (single vcvtph2ps instruction on x86-64 AVX).
// ---------------------------------------------------------------------------
struct WeatherBuffer {
    // Wave grid variables (621 x 1440, -75°..+80°, south-first)
    std::vector<_Float16> sigwh;          // significant wave height (combined)
    std::vector<_Float16> wsh;            // wind-sea significant wave height
    std::vector<_Float16> wsp;            // wind-sea period
    std::vector<_Float16> wsd;            // wind-sea direction
    std::vector<_Float16> pwd;            // peak wave direction
    std::vector<_Float16> swell_residual; // non-local swell wave height

    // Wind grid variables (721 x 1440, -90°..+90°, south-first)
    std::vector<_Float16> was;     // wind speed
    std::vector<_Float16> wad;     // wind direction (going-to)

    // Epoch of the first time step (Unix seconds, UTC)
    int64_t base_epoch   = 0;
    int32_t dt_seconds   = 3600;   // hourly
    int32_t n_timesteps  = WX_N_TIMESTEPS;

    // Factory: allocates all vectors to their grid's *_N_TOTAL elements,
    // zero-initialised.  Callers fill the vectors then pass ownership to
    // WeatherManager::update().
    [[nodiscard]] static std::shared_ptr<WeatherBuffer> make_empty()
    {
        auto buf = std::make_shared<WeatherBuffer>();
        const std::size_t n_wave = WAVE_N_TOTAL;
        const std::size_t n_wind = WIND_N_TOTAL;
        buf->sigwh         .assign(n_wave, _Float16{0});
        buf->wsh           .assign(n_wave, _Float16{0});
        buf->wsp           .assign(n_wave, _Float16{0});
        buf->wsd           .assign(n_wave, _Float16{0});
        buf->pwd           .assign(n_wave, _Float16{0});
        buf->swell_residual.assign(n_wave, _Float16{0});
        buf->was           .assign(n_wind, _Float16{0});
        buf->wad           .assign(n_wind, _Float16{0});
        return buf;
    }

    // Typed span accessor for a single variable across all time steps.
    // Returned span lifetime is bounded by this WeatherBuffer.
    [[nodiscard]] std::span<const _Float16>
    view(const std::vector<_Float16>& var) const noexcept
    {
        return std::span<const _Float16>{var.data(), var.size()};
    }
};

// ---------------------------------------------------------------------------
// WeatherManager
//
// Rule of Zero: the only member requiring careful thought is active_, which
// is std::atomic<std::shared_ptr<const WeatherBuffer>>.
//
// C++23 guarantees std::atomic<shared_ptr<T>> is lock-free on platforms
// where the hardware CAS width covers a pointer (x86-64, AArch64).
// No mutex, no spinlock, no hidden contention on the query hot path.
//
// Ownership model:
//   - update() receives a fully-populated shared_ptr<WeatherBuffer> from
//     the ETL thread and atomically replaces active_.
//   - acquire() is called by query threads; it returns a
//     shared_ptr<const WeatherBuffer> whose refcount keeps the buffer alive
//     for the duration of the query, even if update() fires mid-query.
//   - When the last holder of an old buffer releases its shared_ptr, the
//     WeatherBuffer destructor fires automatically — all vectors freed.
//     No manual cleanup, no leak possible.
//
// There is no "standby slot": we simply allocate a new WeatherBuffer on
// each update cycle and let the old one drain.  Peak memory is therefore
// (active refcount > 1) × one extra buffer for the overlap period, which
// is bounded by the longest in-flight query — milliseconds at most.
// ---------------------------------------------------------------------------
class WeatherManager {
public:
    // Construct with an initial buffer.  The engine must not serve queries
    // until at least one update() has been called (or the initial buffer
    // is supplied here).
    explicit WeatherManager(
        std::shared_ptr<const WeatherBuffer> initial = nullptr) noexcept
        : active_(std::move(initial))
    {}

    // Rule of Zero — compiler-generated destructor drops the shared_ptr,
    // which frees the WeatherBuffer if no query threads still hold it.
    // No user-defined destructor needed.

    // -----------------------------------------------------------------------
    // acquire()
    //
    // Called on every query.  Atomically loads the current active pointer.
    // Cost: one atomic load (memory_order_acquire) + refcount increment.
    // No allocation, no lock.
    //
    // Returns nullptr only if no buffer has been loaded yet; callers must
    // check and return an appropriate error to the API layer.
    // -----------------------------------------------------------------------
    [[nodiscard]] std::shared_ptr<const WeatherBuffer> acquire() const noexcept
    {
#if defined(__cpp_lib_atomic_shared_ptr)
        return active_.load(std::memory_order_acquire);
#else
        return std::atomic_load_explicit(&active_, std::memory_order_acquire);
#endif
    }

    // -----------------------------------------------------------------------
    // update()
    //
    // Called by the ETL thread every 6 hours with a fully-populated buffer.
    // Atomically replaces the active pointer.
    //
    // After the store:
    //   - New queries immediately see the new buffer.
    //   - In-flight queries retain their shared_ptr to the old buffer and
    //     complete normally against it.
    //   - When the last in-flight query releases its shared_ptr, the old
    //     WeatherBuffer is destroyed automatically.
    //
    // update() can be called from any thread; the atomic store is the only
    // synchronization required.
    // -----------------------------------------------------------------------
    void update(std::shared_ptr<const WeatherBuffer> new_buffer) noexcept
    {
#if defined(__cpp_lib_atomic_shared_ptr)
        active_.store(std::move(new_buffer), std::memory_order_release);
#else
        std::atomic_store_explicit(
            &active_, std::move(new_buffer), std::memory_order_release);
#endif
    }

    // Convenience overload — takes a mutable shared_ptr from make_empty()
    // after the ETL has filled it.
    void update(std::shared_ptr<WeatherBuffer> new_buffer) noexcept
    {
#if defined(__cpp_lib_atomic_shared_ptr)
        active_.store(
            std::static_pointer_cast<const WeatherBuffer>(std::move(new_buffer)),
            std::memory_order_release);
#else
        std::atomic_store_explicit(
            &active_,
            std::static_pointer_cast<const WeatherBuffer>(std::move(new_buffer)),
            std::memory_order_release);
#endif
    }

private:
    // C++23 std::atomic<shared_ptr<T>>:
    //   - Lock-free on x86-64 and AArch64 (hardware CAS covers the pointer).
    //   - Standard mandates lock-free if std::atomic<T*>::is_always_lock_free.
    //   - No user-space spinlock, no OS mutex.
#if defined(__cpp_lib_atomic_shared_ptr)
    std::atomic<std::shared_ptr<const WeatherBuffer>> active_;
#else
    std::shared_ptr<const WeatherBuffer> active_;
#endif
};

} // namespace maritime
