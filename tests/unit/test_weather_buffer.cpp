#include "maritime/weather_manager.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

namespace {

using namespace maritime;

TEST(WeatherBuffer, MakeEmptyAllocatesCorrectSize)
{
    auto buf = WeatherBuffer::make_empty();
    ASSERT_NE(buf, nullptr);

    const std::size_t expected_wave = static_cast<std::size_t>(WAVE_N_TOTAL);
    const std::size_t expected_wind = static_cast<std::size_t>(WIND_N_TOTAL);

    EXPECT_EQ(buf->sigwh.size(),          expected_wave);
    EXPECT_EQ(buf->wsh.size(),            expected_wave);
    EXPECT_EQ(buf->wsp.size(),            expected_wave);
    EXPECT_EQ(buf->wsd.size(),            expected_wave);
    EXPECT_EQ(buf->pwd.size(),            expected_wave);
    EXPECT_EQ(buf->swell_residual.size(), expected_wave);
    EXPECT_EQ(buf->was.size(),            expected_wind);
    EXPECT_EQ(buf->wad.size(),            expected_wind);
}

TEST(WeatherBuffer, MakeEmptyZeroInitialised)
{
    auto buf = WeatherBuffer::make_empty();
    // All elements should be zero-initialised (_Float16{0})
    for (std::size_t i = 0; i < buf->sigwh.size(); ++i)
        EXPECT_EQ(static_cast<float>(buf->sigwh[i]), 0.f);
}

TEST(WeatherBuffer, ViewReturnsCorrectSpan)
{
    auto buf = WeatherBuffer::make_empty();
    buf->sigwh[0] = _Float16{1.5f};
    buf->sigwh[1] = _Float16{2.5f};

    auto span = buf->view(buf->sigwh);
    EXPECT_EQ(span.size(), static_cast<std::size_t>(WAVE_N_TOTAL));
    EXPECT_NEAR(static_cast<float>(span[0]), 1.5f, 0.01f);
    EXPECT_NEAR(static_cast<float>(span[1]), 2.5f, 0.01f);
}

// ---------------------------------------------------------------------------
// WeatherManager
// ---------------------------------------------------------------------------

TEST(WeatherManager, NullOnConstruction)
{
    WeatherManager mgr;
    EXPECT_EQ(mgr.acquire(), nullptr);
}

TEST(WeatherManager, AcquireReturnsAfterUpdate)
{
    WeatherManager mgr;
    auto buf = WeatherBuffer::make_empty();
    buf->sigwh[0] = _Float16{3.5f};
    mgr.update(buf);

    auto acquired = mgr.acquire();
    ASSERT_NE(acquired, nullptr);
    EXPECT_NEAR(static_cast<float>(acquired->sigwh[0]), 3.5f, 0.01f);
}

TEST(WeatherManager, AtomicSwapKeepsOldBufferAlive)
{
    WeatherManager mgr;

    auto buf1 = WeatherBuffer::make_empty();
    buf1->sigwh[0] = _Float16{1.0f};
    mgr.update(buf1);

    // Hold a reference to the old buffer
    auto held = mgr.acquire();
    ASSERT_NE(held, nullptr);

    // Swap to a new buffer
    auto buf2 = WeatherBuffer::make_empty();
    buf2->sigwh[0] = _Float16{2.0f};
    mgr.update(buf2);

    // New queries see the new buffer
    auto current = mgr.acquire();
    EXPECT_NEAR(static_cast<float>(current->sigwh[0]), 2.0f, 0.01f);

    // Old reference still valid
    EXPECT_NEAR(static_cast<float>(held->sigwh[0]), 1.0f, 0.01f);
}

TEST(WeatherManager, ConcurrentAcquireIsSafe)
{
    WeatherManager mgr;
    auto buf = WeatherBuffer::make_empty();
    buf->sigwh[42] = _Float16{5.0f};
    mgr.update(buf);

    // Hammer acquire() from many threads simultaneously
    constexpr int N_THREADS = 16;
    constexpr int N_ITERS   = 1000;

    std::vector<std::thread> threads;
    threads.reserve(N_THREADS);

    std::atomic<int> failures{0};

    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < N_ITERS; ++i) {
                auto b = mgr.acquire();
                if (!b) { ++failures; continue; }
                const float v = static_cast<float>(b->sigwh[42]);
                if (std::abs(v - 5.0f) > 0.1f) ++failures;
            }
        });
    }

    for (auto& th : threads) th.join();
    EXPECT_EQ(failures.load(), 0);
}

TEST(WeatherManager, ConcurrentUpdateAndAcquire)
{
    // Writer thread swaps buffers rapidly; reader threads must always get
    // a valid (non-null, finite value) buffer.
    WeatherManager mgr;

    auto buf0 = WeatherBuffer::make_empty();
    buf0->sigwh[0] = _Float16{1.0f};
    mgr.update(buf0);

    std::atomic<bool>  stop{false};
    std::atomic<int>   errors{0};

    // Writer
    std::thread writer([&] {
        float val = 1.0f;
        while (!stop.load()) {
            auto b = WeatherBuffer::make_empty();
            b->sigwh[0] = static_cast<_Float16>(val);
            mgr.update(b);
            val = (val >= 10.f) ? 1.f : val + 1.f;
            std::this_thread::yield();
        }
    });

    // Readers
    std::vector<std::thread> readers;
    for (int i = 0; i < 8; ++i) {
        readers.emplace_back([&] {
            for (int j = 0; j < 2000; ++j) {
                auto b = mgr.acquire();
                if (!b) { ++errors; continue; }
                const float v = static_cast<float>(b->sigwh[0]);
                if (v < 1.f || v > 10.f) ++errors;
            }
        });
    }

    for (auto& r : readers) r.join();
    stop.store(true);
    writer.join();

    EXPECT_EQ(errors.load(), 0);
}

TEST(WeatherBuffer, GridConstantsAreConsistent)
{
    EXPECT_EQ(WAVE_NJ, 621);
    EXPECT_EQ(WIND_NJ, 721);
    EXPECT_EQ(WX_NI, 1440);
    EXPECT_EQ(WAVE_N_POINTS, WAVE_NJ * WX_NI);
    EXPECT_EQ(WIND_N_POINTS, WIND_NJ * WX_NI);
    EXPECT_EQ(WAVE_N_TOTAL, WX_N_TIMESTEPS * WAVE_N_POINTS);
    EXPECT_EQ(WIND_N_TOTAL, WX_N_TIMESTEPS * WIND_N_POINTS);
    EXPECT_EQ(WX_N_TIMESTEPS, 24);
}

} // namespace
