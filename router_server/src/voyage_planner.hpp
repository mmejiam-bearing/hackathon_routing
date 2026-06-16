#pragma once

#include "maritime/edge_weight.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::router_server {

// One simulated day of a rolling-horizon voyage — see plan_voyage() below.
struct DaySegment {
    int                day     = 0;
    float              dist_nm = 0.f;
    float              foc_mt  = 0.f;
    float              time_h  = 0.f;
    std::vector<float> lat;
    std::vector<float> lon;
};

struct VoyageRequest {
    std::string              graph_path;
    std::string              flags_path;
    std::string              snap_wave_path;
    std::string              snap_wind_path;
    std::string              cch_path;
    std::string              avg_weather_dir;   // contains <YYYY>/<MM>/<DD>/<field>.npy
    int                      start_year  = 0;
    int                      start_month = 0;
    int                      start_day   = 0;
    float                    from_lat = 0.f, from_lon = 0.f;
    float                    to_lat   = 0.f, to_lon   = 0.f;
    maritime::VesselParams   vessel;
    float                    sigma_along_nm = 200.f;
    float                    sigma_perp_nm  = 400.f;
    int                      max_periods    = 366;   // hard safety limit (1 year of daily periods)
};

struct VoyagePlan {
    std::vector<DaySegment> segments;
    float                   total_dist_nm = 0.f;
    float                   total_foc_mt  = 0.f;
    float                   total_time_h  = 0.f;
    bool                    reached_destination = false;
};

// Rolling-horizon multi-day routing — the same algorithm as the
// maritime-voyage-router CLI (router_server/src/voyage_router.cpp), factored
// out so it can also be called from the Python bindings. For each forecast
// period: compute that period's calendar date (start_year/month/day +
// period_idx days), load that date's daily-average weather
// (AvgWeatherLoader, mapped onto the 2024 dataset), re-customise the CCH
// with the blended edge weights (WeightsWriter::compute_blended), route from
// the vessel's current position to the destination, advance ~24h along the
// result, repeat.
[[nodiscard]] VoyagePlan plan_voyage(const VoyageRequest& req);

} // namespace maritime::router_server
