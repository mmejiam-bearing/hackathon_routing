// Smoke test: loads all artifacts, finds nearest nodes to two ports,
// runs a route query, and prints the result.
//
// Usage:
//   maritime-route-query --graph <path> --flags <path> --snap <path>
//                        --cch <path> --weights <dir> [--npy <dir>]
//                        --from-lat <f> --from-lon <f>
//                        --to-lat <f>   --to-lon <f>

#include "server.hpp"

#include "maritime/static_graph.hpp"
#include "maritime/edge_weight.hpp"
#include "maritime/foc_model.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

// Find the graph node closest to (lat, lon) by Euclidean distance in degree space.
static uint32_t nearest_node(const maritime::StaticGraph& g, float lat, float lon)
{
    uint32_t best = 0;
    float    best_d2 = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < g.n_nodes(); ++i) {
        const float dlat = g.lat()[i] - lat;
        float dlon = g.lon()[i] - lon;
        // wrap [-180,180]
        if (dlon >  180.f) dlon -= 360.f;
        if (dlon < -180.f) dlon += 360.f;
        const float d2 = dlat*dlat + dlon*dlon;
        if (d2 < best_d2) { best_d2 = d2; best = i; }
    }
    return best;
}

int main(int argc, char** argv)
{
    maritime::router_server::ServerConfig cfg;
    float from_lat = 51.9f, from_lon =  4.5f;  // Rotterdam
    float to_lat   =  1.3f, to_lon  = 103.8f;  // Singapore

    for (int i = 1; i < argc - 1; i += 2) {
        std::string k{argv[i]}, v{argv[i+1]};
        if      (k == "--graph")    cfg.graph_path    = v;
        else if (k == "--flags")    cfg.flags_path    = v;
        else if (k == "--snap")     cfg.snap_path     = v;
        else if (k == "--cch")      cfg.cch_topo_path = v;
        else if (k == "--weights")  cfg.weights_dir   = v;
        else if (k == "--npy")      cfg.npy_dir       = v;
        else if (k == "--from-lat") from_lat = std::stof(v);
        else if (k == "--from-lon") from_lon = std::stof(v);
        else if (k == "--to-lat")   to_lat   = std::stof(v);
        else if (k == "--to-lon")   to_lon   = std::stof(v);
        else { std::cerr << "Unknown arg: " << k << "\n"; return 1; }
    }

    if (cfg.graph_path.empty()) {
        std::cerr << "Usage: maritime-route-query --graph ... --flags ... --snap ... "
                     "--cch ... --weights ...\n";
        return 1;
    }

    try {
        // Build server (loads graph, reconstructs CCH, loads weights + weather)
        cfg.poll_interval_s = 0;
        maritime::router_server::QueryServer server(cfg);

        // Find nodes nearest to requested ports
        maritime::StaticGraph g(cfg.graph_path, cfg.flags_path, cfg.snap_path);
        const uint32_t src = nearest_node(g, from_lat, from_lon);
        const uint32_t dst = nearest_node(g, to_lat,   to_lon);

        std::cout << "Origin node " << src << " at ("
                  << g.lat()[src] << ", " << g.lon()[src] << ")\n";
        std::cout << "Dest   node " << dst << " at ("
                  << g.lat()[dst] << ", " << g.lon()[dst] << ")\n";

        maritime::VesselParams vessel;
        vessel.draft_m            = 10.f;
        vessel.beam_m             = 32.f;
        vessel.loa_m              = 200.f;
        vessel.service_speed_kts  = 14.f;
        vessel.block_coeff        = 0.80f;
        vessel.displacement_t     = 50000.f;
        vessel.transverse_area_m2 = 600.f;
        vessel.foc_model = [spd = vessel.service_speed_kts]
            (float /*sig_wh*/, float wind_spd, float /*curr*/)
            -> std::pair<float,float> {
            return maritime::universal_foc_model(wind_spd, spd);
        };

        maritime::CchRouteRequest req{
            .origin_node    = src,
            .dest_node      = dst,
            .base_time_step = 0,
            .vessel         = std::move(vessel),
        };

        const auto result = server.serve_query(req);

        switch (result.status) {
        case maritime::CchRouteResult::Status::Ok:
            std::cout << "# waypoints=" << result.node_path.size()
                      << " dist_nm=" << result.total_dist_nm
                      << " foc_mt="  << result.total_foc_mt  << "\n";
            std::cout << "lat,lon\n";
            for (std::size_t i = 0; i < result.waypoint_lat.size(); ++i)
                std::cout << result.waypoint_lat[i] << ","
                          << result.waypoint_lon[i] << "\n";
            break;
        case maritime::CchRouteResult::Status::NoRoute:
            std::cerr << "No route found\n"; return 1;
        case maritime::CchRouteResult::Status::WeatherUnavailable:
            std::cerr << "Weather/weights not loaded\n"; return 1;
        }

        server.stop();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return 1;
    }
}
