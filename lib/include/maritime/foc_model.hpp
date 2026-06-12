#pragma once

#include <utility>

namespace maritime {

// ---------------------------------------------------------------------------
// beaufort_from_knots
//
// Returns the Beaufort scale number (0–12) for a given wind speed in knots.
// Thresholds match the WMO Beaufort scale boundary values.
// ---------------------------------------------------------------------------
[[nodiscard]] inline int beaufort_from_knots(float wind_kts) noexcept {
    constexpr float thresholds[] = {
        0.f, 1.f, 3.5f, 6.5f, 10.5f, 16.5f, 21.5f,
        27.5f, 33.5f, 40.5f, 47.5f, 55.5f, 63.5f
    };
    int bf = -1;
    for (float t : thresholds) {
        if (wind_kts >= t) ++bf;
        else break;
    }
    return bf;  // 0–12
}

// ---------------------------------------------------------------------------
// beaufort_from_ms
//
// Convenience wrapper: converts wind speed from m/s to knots, then delegates
// to beaufort_from_knots.
// ---------------------------------------------------------------------------
[[nodiscard]] inline int beaufort_from_ms(float wind_spd_ms) noexcept {
    constexpr float KTS_PER_MS = 1.94384f;
    return beaufort_from_knots(wind_spd_ms * KTS_PER_MS);
}

// ---------------------------------------------------------------------------
// universal_foc_model
//
// Universal FOC model for a laden vessel at reference dimensions, derived
// from regression analysis with all vessel geometry baked into constants.
//
// The model first computes Shaft Horsepower (SHP) in kW:
//
//   SHP (kW) = B0 × (1 + A2 × beaufort³) × speed_kts³ + C_OFFSET
//
// where B0 = (A0 × K_ship) / 1e6 already incorporates the 1×10⁶
// denominator from the original universal FOC formula.
//
// SHP is then converted to fuel consumption via SFOC (Specific Fuel Oil
// Consumption, g/kWh) — a property of the main engine:
//
//   FOC (MT/h) = SHP (kW) × SFOC (g/kWh) / 1,000,000
//   foc_per_nm  = FOC (MT/h) / speed_kts
//
// Parameters
//   wind_spd_ms        — wind speed from weather grid [m/s]
//   service_speed_kts  — vessel design/service speed [knots]
//
// Returns {speed_kts, foc_per_nm_mt}
// ---------------------------------------------------------------------------
[[nodiscard]] inline std::pair<float, float>
universal_foc_model(float wind_spd_ms, float service_speed_kts) noexcept {
    // Regression constants (laden vessel, reference dimensions)
    constexpr float B0       = 76.1394f;    // (A0 × K_ship) / 1e6, speed in knots
    constexpr float A2       = 6.1587e-4f;  // Beaufort cubic weather coefficient
    constexpr float C_OFFSET = 2484.81f;    // a11 × LOA_ref × Beam_ref [kW]

    // Slow-speed two-stroke marine diesel SFOC [g/kWh]
    constexpr float SFOC = 180.0f;

    const int   bf      = beaufort_from_ms(wind_spd_ms);
    const float spd3    = service_speed_kts * service_speed_kts * service_speed_kts;
    const float shp_kw  = B0 * (1.f + A2 * static_cast<float>(bf * bf * bf)) * spd3
                        + C_OFFSET;

    const float foc_mt_h   = shp_kw * SFOC / 1'000'000.f;
    const float foc_per_nm = foc_mt_h / service_speed_kts;

    return {service_speed_kts, foc_per_nm};
}

} // namespace maritime
