//
// maritime-voyage-router
//
// Rolling-horizon multi-day routing. For each 24 h period:
//   1. Load the corresponding forecast npy directory (or reuse the last one
//      when forecasts are exhausted).
//   2. Re-customize the CCH with that period's edge weights.
//   3. Route from the vessel's current position to the destination.
//   4. Advance 24 h along the result at the vessel's nominal speed.
//   5. Repeat until destination is reached.
//
// Usage:
//   maritime-voyage-router
//     --graph  <path>
//     --flags  <path>
//     --snap   <path>
//     --cch    <path>
//     --npy    <dir>   (repeat for each forecast period, chronological order)
//     --from-lat <f>  --from-lon <f>
//     --to-lat   <f>  --to-lon   <f>
//     [--speed   <knots>]   default 12.0
//     [--out     <path>]    default voyage_rolling.geojson
//
#include "maritime/routing_engine.hpp"
#include "maritime/static_graph.hpp"
#include "maritime/edge_weight.hpp"
#include "maritime/foc_model.hpp"
#include "maritime/weather_manager.hpp"

#include "npy_loader.hpp"
#include "ocs_loader.hpp"
#include "weights_writer.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// O(n) nearest-node scan, same approach as route_query.cpp.
uint32_t nearest_node(const maritime::StaticGraph& g, float lat, float lon)
{
    uint32_t best   = 0;
    float    best2  = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < g.n_nodes(); ++i) {
        const float dlat = g.lat()[i] - lat;
        float       dlon = g.lon()[i] - lon;
        if (dlon >  180.f) dlon -= 360.f;
        if (dlon < -180.f) dlon += 360.f;
        const float d2 = dlat*dlat + dlon*dlon;
        if (d2 < best2) { best2 = d2; best = i; }
    }
    return best;
}

// Replicate the npy-loading pattern from weather_etl/src/main.cpp.
std::shared_ptr<maritime::WeatherBuffer> load_npy_dir(const std::string& dir)
{
    auto buf = maritime::WeatherBuffer::make_empty();

    auto copy_npy = [&](std::vector<_Float16>& dst, const char* var) {
        auto raw = maritime::weather_etl::NpyLoader::load(dir + "/" + var + ".npy");
        std::memcpy(dst.data(), raw.data(),
                    static_cast<std::size_t>(maritime::WX_N_POINTS) * sizeof(uint16_t));
    };

    copy_npy(buf->sigwh, "sigwh");
    copy_npy(buf->pwh,   "pwh");
    copy_npy(buf->pwp,   "pwp");
    copy_npy(buf->pwd,   "pwd");
    copy_npy(buf->pswh,  "pswh");
    copy_npy(buf->pswp,  "pswp");
    copy_npy(buf->pswd,  "pswd");
    copy_npy(buf->wsh,   "wsh");
    copy_npy(buf->wsp,   "wsp");
    copy_npy(buf->was,   "was");
    copy_npy(buf->wad,   "wad");
    copy_npy(buf->wsd,   "wsd");

    auto uv = maritime::weather_etl::OcsLoader::load(
        dir + "/ocs.npy", dir + "/ocd.npy");
    for (std::size_t k = 0;
         k < static_cast<std::size_t>(maritime::WX_N_POINTS); ++k) {
        buf->ocs_u[k] = static_cast<_Float16>(uv.u[k]);
        buf->ocs_v[k] = static_cast<_Float16>(uv.v[k]);
    }

    return buf;
}

struct DaySegment {
    int                day     = 0;
    float              dist_nm = 0.f;
    float              foc_mt  = 0.f;
    float              time_h  = 0.f;
    std::vector<float> lat;
    std::vector<float> lon;
};

void write_geojson(const std::vector<DaySegment>& segs, const std::string& path)
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

} // namespace

int main(int argc, char** argv)
{
    std::string              graph_path, flags_path, snap_path, cch_path;
    std::vector<std::string> npy_dirs;
    float from_lat  = 51.9f, from_lon = 4.5f;
    float to_lat    =  1.3f, to_lon   = 103.8f;
    float speed_kts = 14.f;   // sets vessel.service_speed_kts; actual per-edge speed derived from physics
    std::string out_path = "voyage_rolling.geojson";

    for (int i = 1; i < argc; ++i) {
        std::string k{argv[i]};
        if (k == "--npy") {
            if (i + 1 >= argc) { std::cerr << "Missing value for --npy\n"; return 1; }
            npy_dirs.push_back(argv[++i]);
            continue;
        }
        if (i + 1 >= argc) { std::cerr << "Missing value for " << k << "\n"; return 1; }
        std::string v{argv[++i]};
        if      (k == "--graph")    graph_path = v;
        else if (k == "--flags")    flags_path = v;
        else if (k == "--snap")     snap_path  = v;
        else if (k == "--cch")      cch_path   = v;
        else if (k == "--from-lat") from_lat   = std::stof(v);
        else if (k == "--from-lon") from_lon   = std::stof(v);
        else if (k == "--to-lat")   to_lat     = std::stof(v);
        else if (k == "--to-lon")   to_lon     = std::stof(v);
        else if (k == "--speed")    speed_kts  = std::stof(v);
        else if (k == "--out")      out_path   = v;
        else { std::cerr << "Unknown arg: " << k << "\n"; return 1; }
    }

    if (graph_path.empty() || npy_dirs.empty()) {
        std::cerr <<
            "Usage: maritime-voyage-router\n"
            "  --graph <path> --flags <path> --snap <path> --cch <path>\n"
            "  --npy <dir> [--npy <dir> ...]   (one per forecast period)\n"
            "  --from-lat <f> --from-lon <f> --to-lat <f> --to-lon <f>\n"
            "  [--speed <knots>] [--out <path>]\n";
        return 1;
    }

    try {
        maritime::RoutingEngine       engine(graph_path, flags_path, snap_path, cch_path);
        const maritime::StaticGraph& graph = engine.graph();

        const uint32_t src = nearest_node(graph, from_lat, from_lon);
        const uint32_t dst = nearest_node(graph, to_lat,   to_lon);

        std::cout << "[voyage] Origin node " << src
                  << " at (" << graph.lat()[src] << ", " << graph.lon()[src] << ")\n"
                  << "[voyage] Dest   node " << dst
                  << " at (" << graph.lat()[dst] << ", " << graph.lon()[dst] << ")\n"
                  << "[voyage] " << npy_dirs.size()
                  << " forecast period(s) available\n\n";

        maritime::VesselParams vessel;
        vessel.draft_m            = 10.f;
        vessel.beam_m             = 32.f;
        vessel.loa_m              = 200.f;
        vessel.service_speed_kts  = speed_kts;
        vessel.block_coeff        = 0.80f;
        vessel.displacement_t     = 50000.f;
        vessel.transverse_area_m2 = 600.f;
        vessel.foc_model = [spd = vessel.service_speed_kts]
            (float /*sig_wh*/, float wind_spd, float /*curr*/)
            -> std::pair<float, float> {
            return maritime::universal_foc_model(wind_spd, spd);
        };

        uint32_t current_node = src;
        int      period_idx   = 0;
        int      last_loaded  = -1;
        std::shared_ptr<maritime::WeatherBuffer> current_wx;
        std::vector<DaySegment> segments;
        float total_dist = 0.f, total_foc = 0.f;

        constexpr int MAX_PERIODS = 366;   // hard safety limit (1 year of daily periods)

        while (current_node != dst && period_idx < MAX_PERIODS) {
            const int effective = std::min(period_idx,
                                           static_cast<int>(npy_dirs.size()) - 1);

            if (period_idx == static_cast<int>(npy_dirs.size())) {
                std::cout << "[voyage] Forecast exhausted after day "
                          << (npy_dirs.size() - 1)
                          << " — extending with last available weather.\n";
            }

            if (effective != last_loaded) {
                std::cout << "[voyage] Loading forecast " << npy_dirs[effective]
                          << " for day " << period_idx << " ...\n";
                current_wx = load_npy_dir(npy_dirs[effective]);
                auto weights = maritime::weather_etl::WeightsWriter::compute(
                    graph, *current_wx, 0, &vessel);
                engine.update_weights(std::move(weights));
                engine.update_weather(current_wx);
                last_loaded = effective;
            }

            const uint32_t seg_start = current_node;

            maritime::CchRouteRequest req{
                .origin_node    = current_node,
                .dest_node      = dst,
                .base_time_step = 0,
                .vessel         = vessel,
            };
            const auto result = engine.route(req);

            if (result.status != maritime::CchRouteResult::Status::Ok) {
                std::cerr << "[voyage] Route failed on day " << period_idx << "\n";
                return 1;
            }

            DaySegment seg;
            seg.day = period_idx;

            float elapsed_h = 0.f;

            for (std::size_t i = 0; i + 1 < result.node_path.size(); ++i) {
                const uint32_t from = result.node_path[i];
                const uint32_t to   = result.node_path[i + 1];

                const float d_nm = maritime::haversine_nm(
                    graph.lat()[from], graph.lon()[from],
                    graph.lat()[to],   graph.lon()[to]);

                seg.lat.push_back(graph.lat()[from]);
                seg.lon.push_back(graph.lon()[from]);

                maritime::EdgeCost ec{0.f, d_nm / vessel.service_speed_kts};
                if (current_wx) {
                    const int ts = std::clamp(static_cast<int>(i),
                                              0, maritime::WX_N_TIMESTEPS - 1);
                    ec = maritime::compute_edge_cost(
                        from, to, ts, graph, *current_wx, vessel);
                }
                seg.foc_mt  += ec.foc_mt;
                seg.time_h  += ec.time_h;
                seg.dist_nm += d_nm;
                elapsed_h   += ec.time_h;
                current_node = to;

                if (to == dst) {
                    seg.lat.push_back(graph.lat()[to]);
                    seg.lon.push_back(graph.lon()[to]);
                    break;
                }

                if (elapsed_h >= 24.f)
                    break;
            }

            std::cout << "[voyage] Day " << period_idx
                      << ": " << seg.dist_nm << " nm"
                      << "  (" << graph.lat()[seg_start]  << ", "
                                << graph.lon()[seg_start]  << ")"
                      << " → (" << graph.lat()[current_node] << ", "
                                 << graph.lon()[current_node] << ")\n";

            total_dist += seg.dist_nm;
            total_foc  += seg.foc_mt;
            segments.push_back(std::move(seg));
            ++period_idx;
        }

        if (current_node != dst)
            std::cerr << "[voyage] Warning: destination not reached after "
                      << period_idx << " periods.\n";

        float total_time = 0.f;
        for (const auto& s : segments) total_time += s.time_h;
        std::cout << "\n[voyage] Complete: " << segments.size() << " day(s)"
                  << "  total_dist=" << total_dist << " nm"
                  << "  total_foc="  << total_foc  << " mt"
                  << "  total_time=" << total_time  << " h\n";

        write_geojson(segments, out_path);
        std::cout << "[voyage] Written: " << out_path << "\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return 1;
    }
}
