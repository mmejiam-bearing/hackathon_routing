//
// maritime-voyage-router
//
// Rolling-horizon multi-day routing. For each 24 h period:
//   1. Compute that period's calendar date (start-date + days elapsed) and
//      load its daily-average weather (mapped onto the 2024 dataset).
//   2. Re-customize the CCH with that period's edge weights.
//   3. Route from the vessel's current position to the destination.
//   4. Advance 24 h along the result at the vessel's nominal speed.
//   5. Repeat until destination is reached.
//
// The algorithm itself lives in voyage_planner.{hpp,cpp} so it can also be
// called from the Python bindings (router_server/python/bindings.cpp) — this
// file is just argument parsing and GeoJSON output.
//
// Usage:
//   maritime-voyage-router
//     --graph           <path>
//     --flags           <path>
//     --snap-wave       <path>
//     --snap-wind       <path>
//     --cch             <path>
//     --avg-weather-dir <dir>    contains <YYYY>/<MM>/<DD>/<field>.npy
//     --start-date      <YYYY-MM-DD>
//     --from-lat <f>  --from-lon <f>
//     --to-lat   <f>  --to-lon   <f>
//     [--speed   <knots>]   default 12.0
//     [--out     <path>]    default voyage_rolling.geojson
//
#include "voyage_planner.hpp"

#include "maritime/edge_weight.hpp"
#include "maritime/foc_model.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void write_geojson(const std::vector<maritime::router_server::DaySegment>& segs,
                    const std::string& path)
{
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot open output: " + path);

    f << "{\n  \"type\": \"FeatureCollection\",\n  \"features\": [\n";

    for (std::size_t si = 0; si < segs.size(); ++si) {
        const auto& s = segs[si];
        f << "    {\n"
          << "      \"type\": \"Feature\",\n"
          << "      \"properties\": {"
          << " \"day\": "     << s.day      << ","
          << " \"dist_nm\": " << s.dist_nm  << ","
          << " \"foc_mt\": "  << s.foc_mt   << ","
          << " \"time_h\": "  << s.time_h
          << " },\n"
          << "      \"geometry\": {\n"
          << "        \"type\": \"LineString\",\n"
          << "        \"coordinates\": [\n";

        for (std::size_t i = 0; i < s.lat.size(); ++i) {
            f << "          [" << s.lon[i] << ", " << s.lat[i] << "]";
            if (i + 1 < s.lat.size()) f << ",";
            f << "\n";
        }

        f << "        ]\n      }\n    }";
        if (si + 1 < segs.size()) f << ",";
        f << "\n";
    }

    f << "  ]\n}\n";
    if (!f) throw std::runtime_error("Write failed: " + path);
}

// Parses "YYYY-MM-DD" into (year, month, day). Throws on malformed input.
void parse_iso_date(const std::string& s, int& year, int& month, int& day)
{
    int y, m, d;
    char dash1, dash2;
    std::istringstream iss(s);
    iss >> y >> dash1 >> m >> dash2 >> d;
    if (!iss || dash1 != '-' || dash2 != '-')
        throw std::invalid_argument("--start-date must be YYYY-MM-DD, got: " + s);
    year = y; month = m; day = d;
}

} // namespace

int main(int argc, char** argv)
{
    maritime::router_server::VoyageRequest req;
    float speed_kts = 14.f;   // sets vessel.service_speed_kts; actual per-edge speed derived from physics
    std::string out_path = "voyage_rolling.geojson";
    std::string start_date_str;
    req.from_lat = 51.9f; req.from_lon = 4.5f;
    req.to_lat   =  1.3f; req.to_lon   = 103.8f;

    for (int i = 1; i < argc; ++i) {
        std::string k{argv[i]};
        if (i + 1 >= argc) { std::cerr << "Missing value for " << k << "\n"; return 1; }
        std::string v{argv[++i]};
        if      (k == "--graph")           req.graph_path      = v;
        else if (k == "--flags")           req.flags_path      = v;
        else if (k == "--snap-wave")       req.snap_wave_path  = v;
        else if (k == "--snap-wind")       req.snap_wind_path  = v;
        else if (k == "--cch")             req.cch_path        = v;
        else if (k == "--avg-weather-dir") req.avg_weather_dir = v;
        else if (k == "--start-date")      start_date_str      = v;
        else if (k == "--from-lat")        req.from_lat        = std::stof(v);
        else if (k == "--from-lon")        req.from_lon        = std::stof(v);
        else if (k == "--to-lat")          req.to_lat          = std::stof(v);
        else if (k == "--to-lon")          req.to_lon          = std::stof(v);
        else if (k == "--speed")           speed_kts           = std::stof(v);
        else if (k == "--out")             out_path            = v;
        else { std::cerr << "Unknown arg: " << k << "\n"; return 1; }
    }

    if (req.graph_path.empty() || req.avg_weather_dir.empty() || start_date_str.empty()) {
        std::cerr <<
            "Usage: maritime-voyage-router\n"
            "  --graph <path> --flags <path> --snap-wave <path> --snap-wind <path> --cch <path>\n"
            "  --avg-weather-dir <dir>   (contains <YYYY>/<MM>/<DD>/<field>.npy)\n"
            "  --start-date <YYYY-MM-DD>\n"
            "  --from-lat <f> --from-lon <f> --to-lat <f> --to-lon <f>\n"
            "  [--speed <knots>] [--out <path>]\n";
        return 1;
    }

    try {
        parse_iso_date(start_date_str, req.start_year, req.start_month, req.start_day);

        req.vessel.draft_m            = 10.f;
        req.vessel.beam_m             = 32.f;
        req.vessel.loa_m              = 200.f;
        req.vessel.service_speed_kts  = speed_kts;
        req.vessel.block_coeff        = 0.80f;
        req.vessel.displacement_t     = 50000.f;
        req.vessel.transverse_area_m2 = 600.f;
        req.vessel.foc_model = [spd = speed_kts]
            (float /*sig_wh*/, float wind_spd, float /*curr*/)
            -> std::pair<float, float> {
            return maritime::universal_foc_model(wind_spd, spd);
        };

        std::cout << "[voyage] Routing from (" << req.from_lat << ", " << req.from_lon
                  << ") to (" << req.to_lat << ", " << req.to_lon << ") starting "
                  << start_date_str << " (average weather)\n";

        const auto plan = maritime::router_server::plan_voyage(req);

        for (const auto& s : plan.segments)
            std::cout << "[voyage] Day " << s.day << ": " << s.dist_nm << " nm\n";

        if (!plan.reached_destination)
            std::cerr << "[voyage] Warning: destination not reached after "
                      << plan.segments.size() << " period(s).\n";

        std::cout << "\n[voyage] Complete: " << plan.segments.size() << " day(s)"
                  << "  total_dist=" << plan.total_dist_nm << " nm"
                  << "  total_foc="  << plan.total_foc_mt  << " mt"
                  << "  total_time=" << plan.total_time_h  << " h\n";

        write_geojson(plan.segments, out_path);
        std::cout << "[voyage] Written: " << out_path << "\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return 1;
    }
}
