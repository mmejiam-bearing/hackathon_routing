#pragma once

#include "maritime/weather_manager.hpp"

#include <memory>
#include <string>

namespace maritime::weather_etl {

// ---------------------------------------------------------------------------
// AvgWeatherLoader
//
// Loads one calendar day's daily-average weather (see
// average_weather_description.md) into a fully-populated WeatherBuffer.
//
// The average dataset only covers 2024; every other year's date is mapped
// onto 2024's same month/day (2024 is a leap year, so every real calendar
// date — including Feb 29 — has a match, no special casing needed).
//
// base_dir must contain <YYYY>/<MM>/<DD>/<field>.npy for each of the 8
// fields (sigwh, wsh, wsp, wsd, pwd, swell_residual, was, wad), float64,
// NaN = land. Each day's single snapshot is broadcast across all
// WX_N_TIMESTEPS hourly slots (the average dataset has no intra-day
// variation to model).
// ---------------------------------------------------------------------------
struct AvgWeatherLoader {
    [[nodiscard]] static std::shared_ptr<WeatherBuffer> load(
        const std::string& base_dir,
        int year, int month, int day);
};

} // namespace maritime::weather_etl
