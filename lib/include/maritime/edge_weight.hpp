#pragma once

#include "maritime/foc_model.hpp"
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
// The FOC model is a callable stored by value — used for canal-transit
// calm-water FOC; open-ocean edges call universal_foc_model directly with
// the physics-adjusted speed.
// ---------------------------------------------------------------------------
struct VesselParams {
    float draft_m             = 0.f;    // static draft [m]
    float beam_m              = 0.f;    // beam [m]
    float loa_m               = 0.f;    // length overall [m]
    float service_speed_kts   = 14.f;  // design/service speed [kts]
    float block_coeff         = 0.80f; // block coefficient Cb (0.6–0.85)
    float displacement_t      = 50000.f; // laden displacement [tonnes]
    float transverse_area_m2  = 600.f; // frontal area above waterline [m²]

    // Canal-transit calm-water FOC.
    // Returns {speed_kts, foc_per_nm_mt} given met-ocean conditions.
    std::function<
        std::pair<float,float>
        (float sig_wh, float wind_spd, float current_comp)
    > foc_model;
};

// ---------------------------------------------------------------------------
// EdgeCost — returned by compute_edge_cost
// ---------------------------------------------------------------------------
struct EdgeCost {
    float foc_mt = 0.f;   // fuel cost for this edge [MT]
    float time_h = 0.f;   // transit time for this edge [h]
};

// ---------------------------------------------------------------------------
// Haversine distance in nautical miles — constexpr-friendly, branchless.
// ---------------------------------------------------------------------------
[[nodiscard]] inline float haversine_nm(
    float lat1, float lon1,
    float lat2, float lon2) noexcept
{
    constexpr float R_NM  = 3440.065f;
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
// calm_water_resistance_kn
//
// ITTC 1957 friction line + simplified Hollenbach residual + C_a correction.
// Returns total calm-water towing resistance [kN] at the given speed.
// ---------------------------------------------------------------------------
[[nodiscard]] inline float calm_water_resistance_kn(
    float speed_kts, const VesselParams& v) noexcept
{
    constexpr float NU_SEA = 1.19e-6f;  // kinematic viscosity seawater [m²/s]
    constexpr float RHO    = 1025.f;    // seawater density [kg/m³]

    const float L_wl     = v.loa_m * 0.95f;
    const float vol      = v.displacement_t / 1.025f;      // m³
    const float S_wet    = 2.5f * std::sqrt(vol * L_wl);   // m², Mumford
    const float speed_ms = speed_kts * 0.5144f;

    if (speed_ms < 1e-4f) return 0.f;

    // ITTC 1957 frictional resistance coefficient
    const float Re    = speed_ms * L_wl / NU_SEA;
    const float logRe = std::log10(Re);
    const float C_f   = 0.075f / ((logRe - 2.f) * (logRe - 2.f));

    // Incremental resistance coefficient
    const float logD  = std::log10(v.displacement_t);
    const float C_a   = (0.5f * logD - 0.1f * logD * logD) / 1000.f;

    // Hollenbach residual resistance coefficient
    const float cbrt_vol = std::cbrt(vol);
    const float L_vol    = L_wl / cbrt_vol;
    const float Fn       = speed_ms / std::sqrt(9.81f * v.loa_m);
    const float Cp       = v.block_coeff;

    const float A0   = 1.35f - 0.23f * L_vol + 0.012f * L_vol * L_vol;
    const float A1   = 0.0011f * std::pow(L_vol, 9.1f);
    const float N1   = 2.f * L_vol - 3.7f;

    const float lv2   = L_vol - 2.f;
    const float lv2_4 = std::max(lv2 * lv2 * lv2 * lv2, 1e-6f);

    const float E = (A0 + 1.5f * std::pow(Fn, 1.8f) + A1 * std::pow(Fn, N1))
                  * (0.98f + 2.5f / lv2_4)
                  + std::pow(L_vol - 5.f, 4.f) * std::pow(Fn - 0.1f, 4.f);

    const float B1   = 7.f - 0.09f * L_vol * L_vol;
    const float B2   = (5.f * Cp - 2.5f) * (5.f * Cp - 2.5f);
    const float B3   = std::pow(600.f * (Fn - 0.315f) * (Fn - 0.315f) + 1.f, 1.5f);
    const float G_h  = (B1 * B2) / B3;

    // Clamp exponents to prevent overflow on extreme inputs
    const float H = std::exp(std::clamp(
        80.f * (Fn - (0.04f + 0.59f * Cp) - 0.015f * (L_vol - 5.f)),
        -100.f, 0.f));  // H only contributes near hump speed; clamp to [e^-100,1]
    const float K = 180.f * std::pow(std::max(Fn, 1e-6f), 3.7f)
                  * std::exp(std::clamp(20.f * Cp - 16.f, -20.f, 20.f));

    const float C_r_raw  = (E + G_h + H + K) / 1000.f;
    const float b_over_t = (v.draft_m > 0.1f) ? (v.beam_m / v.draft_m) : 3.2f;
    const float C_r_bd   = 0.16f * (b_over_t - 2.5f) / 1000.f;
    const float C_r      = (C_r_raw + C_r_bd) * 0.7f;

    // 0.5 * ρ * V² * S_wet * C gives Newtons; divide by 1000 for kN
    return 0.5f * RHO * speed_ms * speed_ms * S_wet * (C_f + C_r + C_a) / 1000.f;
}

// ---------------------------------------------------------------------------
// wave_resistance_kn
//
// Kreitner's method.  Head-wave resistance scaled by an 8-segment angle table
// (0° = head-on, 180° = following sea).  Result in kN.
// ---------------------------------------------------------------------------
[[nodiscard]] inline float wave_resistance_kn(
    float sig_wh, float wave_rel_deg, const VesselParams& v) noexcept
{
    constexpr float angles[9] = {0.f, 22.5f, 45.f, 67.5f, 90.f, 112.5f, 135.f, 157.5f, 180.f};
    constexpr float coeffs[9] = {1.0f, 1.125f, 1.0f, 0.75f, 0.425f, 0.4f, 0.3f, 0.2f, 0.1f};

    const float clamped = std::clamp(wave_rel_deg, 0.f, 180.f);
    float angle_coeff = coeffs[8];
    for (int i = 0; i < 8; ++i) {
        if (clamped <= angles[i + 1]) {
            const float t = (clamped - angles[i]) / (angles[i + 1] - angles[i]);
            angle_coeff = coeffs[i] + t * (coeffs[i + 1] - coeffs[i]);
            break;
        }
    }

    // Kreitner head-wave resistance [N] → [kN]
    const float R_head = 0.64f * sig_wh * sig_wh
                       * v.beam_m * v.beam_m
                       * v.block_coeff
                       * (1025.f * 9.81f) / v.loa_m / 1000.f;

    return angle_coeff * R_head;
}

// ---------------------------------------------------------------------------
// wind_resistance_kn
//
// Simplified Fujiwara aerodynamic resistance.
// Relative wind is the vector sum of true wind and vessel motion.
// C_wind ≈ 0.5 × cos(θ) for head-on, zero for beam/stern.  Result in kN.
// ---------------------------------------------------------------------------
[[nodiscard]] inline float wind_resistance_kn(
    float was_ms, float wind_rel_deg, float speed_kts,
    const VesselParams& v) noexcept
{
    if (was_ms < 1e-6f) return 0.f;

    const float vog_ms  = speed_kts * 0.5144f;
    const float rel_rad = wind_rel_deg * (std::numbers::pi_v<float> / 180.f);
    const float cos_rel = std::cos(rel_rad);
    const float sin_rel = std::sin(rel_rad);

    // Relative wind vector components
    const float vx       = was_ms * cos_rel + vog_ms;
    const float vy       = was_ms * sin_rel;
    const float V_rel_sq = vx * vx + vy * vy;

    const float C_wind = std::max(0.f, cos_rel) * 0.5f;

    // 0.5 * ρ_air * V_rel² * C_wind * A_T gives Newtons; divide by 1000 for kN
    return 0.5f * 1.225f * V_rel_sq * C_wind * v.transverse_area_m2 / 1000.f;
}

// ---------------------------------------------------------------------------
// speed_loss_pct
//
// Kwon estimation (combined weather).
// Equal-power assumption: 100 × (√(1 + ΔR/R_cw) − 1).  Capped at 100%.
// ---------------------------------------------------------------------------
[[nodiscard]] inline float speed_loss_pct(
    float R_cw, float delta_R) noexcept
{
    if (R_cw < 1e-6f || delta_R <= 0.f) return 0.f;
    return std::min(100.f, 100.f * (std::sqrt(1.f + delta_R / R_cw) - 1.f));
}

// ---------------------------------------------------------------------------
// compute_edge_cost
//
// The hot path.  Called on every edge of the extracted route.
// Returns {foc_mt, time_h} for the edge.
//
// Speed-loss physics (Hollenbach + Kreitner + Kwon) reduce the vessel's
// effective speed below service speed; universal_foc_model is then evaluated
// at that actual speed so both FOC and ETA reflect heavy-weather conditions.
//
// Direction convention: wad and pwd are "going-to" [°].  Both are converted
// to "coming-from" before computing the vessel-relative angle, so that
//   0° = head-on (worst resistance), 180° = following (minimum resistance).
// ---------------------------------------------------------------------------
[[nodiscard]] inline EdgeCost compute_edge_cost(
    uint32_t                    from,
    uint32_t                    to,
    int                         time_step,
    const StaticGraph&          graph,
    const WeatherBuffer&        wx,
    const VesselParams&         vessel) noexcept
{
    constexpr EdgeCost INFEASIBLE{1e9f, 0.f};

    const uint8_t from_flags = graph.flags()[from];

    // ------------------------------------------------------------------
    // Hard feasibility checks — prune before any arithmetic
    // ------------------------------------------------------------------
    const float depth_m = static_cast<float>(graph.depth()[from]);
    if (depth_m < vessel.draft_m + 1.5f)
        return INFEASIBLE;

    if (from_flags & FLAG_RESTRICTED)
        return INFEASIBLE;

    // ------------------------------------------------------------------
    // Great-circle distance — computed from lat/lon to avoid the
    // per-edge base_dist array being mis-indexed by node id.
    // ------------------------------------------------------------------
    const float dist_nm = haversine_nm(
        graph.lat()[from], graph.lon()[from],
        graph.lat()[to],   graph.lon()[to]);

    // ------------------------------------------------------------------
    // Canal / enclosed waterway — calm-water FOC, no weather
    // ------------------------------------------------------------------
    if (from_flags & FLAG_CANAL_TRANSIT) {
        const auto [spd, foc_per_nm] = vessel.foc_model(0.f, 0.f, 0.f);
        const float safe_spd = std::max(spd, 0.1f);
        return EdgeCost{foc_per_nm * dist_nm, dist_nm / safe_spd};
    }

    // ------------------------------------------------------------------
    // Weather lookup
    // ------------------------------------------------------------------
    const std::size_t wx_idx = graph.weather_idx(from, time_step);

    const float sig_wh   = static_cast<float>(wx.sigwh[wx_idx]);
    const float wind_spd = static_cast<float>(wx.was  [wx_idx]);
    const float cur_u    = static_cast<float>(wx.ocs_u[wx_idx]);
    const float cur_v    = static_cast<float>(wx.ocs_v[wx_idx]);
    const float wad_deg  = static_cast<float>(wx.wad  [wx_idx]);  // wind going-to [°]
    const float pwd_deg  = static_cast<float>(wx.pwd  [wx_idx]);  // wave going-to [°]

    // ------------------------------------------------------------------
    // Vessel heading
    // ------------------------------------------------------------------
    const float hdg = bearing_rad(
        graph.lat()[from], graph.lon()[from],
        graph.lat()[to],   graph.lon()[to]);

    // Current component projected onto vessel heading (for FOC model)
    const float current_comp = cur_u * std::sin(hdg) + cur_v * std::cos(hdg);

    // ------------------------------------------------------------------
    // Weather-relative angles
    // Convert going-to direction → coming-from, then fold into [0°,180°]
    //   0° = head-on (worst resistance), 180° = following
    // ------------------------------------------------------------------
    constexpr float DEG2R = std::numbers::pi_v<float> / 180.f;
    constexpr float R2DEG = 180.f / std::numbers::pi_v<float>;

    const float wind_from = (wad_deg + 180.f) * DEG2R;
    const float wave_from = (pwd_deg + 180.f) * DEG2R;

    const float wind_rel_deg = std::acos(std::cos(hdg - wind_from)) * R2DEG;
    const float wave_rel_deg = std::acos(std::cos(hdg - wave_from)) * R2DEG;

    // ------------------------------------------------------------------
    // Speed-loss physics
    // ------------------------------------------------------------------
    const float R_cw   = calm_water_resistance_kn(vessel.service_speed_kts, vessel);
    const float R_wave = wave_resistance_kn(sig_wh, wave_rel_deg, vessel);
    const float R_wind = wind_resistance_kn(wind_spd, wind_rel_deg,
                                            vessel.service_speed_kts, vessel);
    const float sl     = speed_loss_pct(R_cw, R_wave + R_wind);

    const float actual_spd = std::max(1.f,
        vessel.service_speed_kts * (1.f - sl / 100.f));

    // ------------------------------------------------------------------
    // FOC at weather-adjusted speed — equal-power model
    // Engine maintains constant output (service-speed rate); vessel moves slower.
    // FOC_hour = universal_foc_model(service_speed).foc_per_nm × service_speed
    // foc_per_nm = FOC_hour / actual_spd
    // ------------------------------------------------------------------
    const float foc_mt_h   = universal_foc_model(wind_spd, vessel.service_speed_kts).second
                             * vessel.service_speed_kts;   // MT/h at constant engine output
    const float foc_per_nm = foc_mt_h / actual_spd;

    // ------------------------------------------------------------------
    // ECA and TSS modifiers (unchanged)
    // ------------------------------------------------------------------
    const float eca_factor =
        1.f + 0.15f * static_cast<float>((from_flags & FLAG_ECA) != 0);
    const float tss_penalty =
        static_cast<float>((from_flags & FLAG_TSS) != 0) * 50.f;

    (void)current_comp;  // available for future current-resistance extension

    return EdgeCost{
        foc_per_nm * dist_nm * eca_factor + tss_penalty,
        dist_nm / actual_spd
    };
}

} // namespace maritime
