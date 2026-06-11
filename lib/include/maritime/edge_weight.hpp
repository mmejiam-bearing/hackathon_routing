#pragma once

#include "maritime/static_graph.hpp"
#include "maritime/weather_manager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <utility>

namespace maritime {

// ---------------------------------------------------------------------------
// VesselParams
//
// Rule of Zero: plain value type, no resources.
// Passed by const-ref to every edge weight computation.
// The FOC model is a callable stored by value — no virtual dispatch,
// no heap allocation.
// ---------------------------------------------------------------------------
struct VesselParams {
    float draft_m   = 0.f;   // static draft in metres
    float beam_m    = 0.f;   // beam in metres
    float loa_m     = 0.f;   // length overall in metres

    // Universal vessel FOC model.
    // Returns {speed_kts, foc_per_nm_mt} given met-ocean conditions
    // and the current component along the vessel heading.
    // Implemented externally — injected here as a std::function so the
    // C++ engine does not depend on the Python model layer.
    std::function<
        std::pair<float,float>   // {speed_kts, foc_per_nm}
        (float sig_wh,           // significant wave height [m]
         float wind_spd,         // wind speed [m/s]
         float current_comp)     // current component along heading [m/s]
    > foc_model;
};

// ---------------------------------------------------------------------------
// Haversine distance in nautical miles — constexpr-friendly, branchless.
// ---------------------------------------------------------------------------
[[nodiscard]] inline float haversine_nm(
    float lat1, float lon1,
    float lat2, float lon2) noexcept
{
    constexpr float R_NM  = 3440.065f;   // Earth radius in nautical miles
    constexpr float DEG2R = std::numbers::pi_v<float> / 180.f;

    const float dlat = (lat2 - lat1) * DEG2R;
    const float dlon = (lon2 - lon1) * DEG2R;
    const float rl1  = lat1 * DEG2R;
    const float rl2  = lat2 * DEG2R;

    const float a = std::sin(dlat * .5f) * std::sin(dlat * .5f)
                  + std::cos(rl1) * std::cos(rl2)
                  * std::sin(dlon * .5f) * std::sin(dlon * .5f);

    return 2.f * R_NM * std::asin(std::sqrt(a));
}

// ---------------------------------------------------------------------------
// Bearing from (lat1,lon1) to (lat2,lon2) in radians, clockwise from north.
// ---------------------------------------------------------------------------
[[nodiscard]] inline float bearing_rad(
    float lat1, float lon1,
    float lat2, float lon2) noexcept
{
    constexpr float DEG2R = std::numbers::pi_v<float> / 180.f;
    const float rl1  = lat1 * DEG2R;
    const float rl2  = lat2 * DEG2R;
    const float dlon = (lon2 - lon1) * DEG2R;

    const float y = std::sin(dlon) * std::cos(rl2);
    const float x = std::cos(rl1) * std::sin(rl2)
                  - std::sin(rl1) * std::cos(rl2) * std::cos(dlon);

    return std::atan2(y, x);   // [-π, π], 0 = north
}

// ---------------------------------------------------------------------------
// compute_edge_cost
//
// The hot path.  Called millions of times per query.
// Design constraints:
//   - No heap allocation.
//   - No branches on the critical path (ECA uses a branchless multiply).
//   - float16 → float32 cast at the arithmetic site only (vcvtph2ps).
//   - Canal-transit nodes bypass weather entirely.
//   - Snap table already applied inside StaticGraph::weather_idx().
//
// Parameters
//   from, to    — graph node indices
//   time_step   — index into [0, WX_N_TIMESTEPS), derived from elapsed
//                 voyage hours so weather advances with the vessel
//   graph       — static graph (memory-mapped, read-only)
//   wx          — weather buffer acquired by the calling query thread
//   vessel      — vessel parameters including FOC model callable
//
// Returns
//   FOC cost in metric tonnes for traversing this edge, or a large
//   sentinel value if the edge is infeasible (depth violation, restricted
//   area without clearance).
// ---------------------------------------------------------------------------
[[nodiscard]] inline float compute_edge_cost(
    uint32_t                    from,
    uint32_t                    to,
    int                         time_step,
    const StaticGraph&          graph,
    const WeatherBuffer&        wx,
    const VesselParams&         vessel) noexcept
{
    constexpr float INFEASIBLE = 1e9f;

    const uint8_t from_flags = graph.flags()[from];

    // ------------------------------------------------------------------
    // Hard feasibility checks — prune before any arithmetic
    // ------------------------------------------------------------------

    // Depth check: graph depth stored as float16 positive metres
    const float depth_m =
        static_cast<float>(graph.depth()[from]);   // vcvtph2ps
    if (depth_m < vessel.draft_m + 1.5f)           // 1.5m UKC minimum
        return INFEASIBLE;

    // Restricted area without override — edge cost is infinite
    if (from_flags & FLAG_RESTRICTED)
        return INFEASIBLE;

    // ------------------------------------------------------------------
    // Great-circle distance (weather-independent, precomputed)
    // ------------------------------------------------------------------
    const float dist_nm = graph.base_dist()[from];   // edge weight pre-baked

    // ------------------------------------------------------------------
    // Canal / enclosed waterway — calm-water FOC, no weather lookup
    // ------------------------------------------------------------------
    if (from_flags & FLAG_CANAL_TRANSIT) {
        const auto [spd, foc_per_nm] = vessel.foc_model(0.f, 0.f, 0.f);
        (void)spd;
        return foc_per_nm * dist_nm;
    }

    // ------------------------------------------------------------------
    // Weather lookup — O(1), snap table already applied in weather_idx()
    // ------------------------------------------------------------------
    const std::size_t wx_idx = graph.weather_idx(from, time_step);

    // float16 → float32 at point of arithmetic only
    const float sig_wh  = static_cast<float>(wx.sigwh [wx_idx]);
    const float wind_spd = static_cast<float>(wx.was  [wx_idx]);
    const float cur_u   = static_cast<float>(wx.ocs_u [wx_idx]);
    const float cur_v   = static_cast<float>(wx.ocs_v [wx_idx]);

    // Current component projected onto vessel heading
    const float hdg = bearing_rad(
        graph.lat()[from], graph.lon()[from],
        graph.lat()[to],   graph.lon()[to]);

    const float current_comp = cur_u * std::sin(hdg)
                              + cur_v * std::cos(hdg);

    // ------------------------------------------------------------------
    // FOC model call — universal vessel model injected via VesselParams
    // ------------------------------------------------------------------
    const auto [speed_kts, foc_per_nm] =
        vessel.foc_model(sig_wh, wind_spd, current_comp);

    if (speed_kts < 0.1f)   // vessel cannot make headway
        return INFEASIBLE;

    // ------------------------------------------------------------------
    // ECA cost multiplier — branchless, no branch predictor pressure
    // Assumes 15% OPEX premium for LSFO/scrubber in ECA zones
    // ------------------------------------------------------------------
    const float eca_factor =
        1.f + 0.15f * static_cast<float>((from_flags & FLAG_ECA) != 0);

    // ------------------------------------------------------------------
    // TSS direction compliance — heading deviation penalty
    // Vessels violating TSS direction get a heavy cost, not infeasibility,
    // so the solver can still route through if no alternative exists
    // ------------------------------------------------------------------
    const float tss_penalty =
        static_cast<float>((from_flags & FLAG_TSS) != 0) * 50.f;

    return foc_per_nm * dist_nm * eca_factor + tss_penalty;
}

} // namespace maritime
