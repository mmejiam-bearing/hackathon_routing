#include "graph_builder.hpp"

#include "canal_injector.hpp"
#include "gebco_loader.hpp"
#include "gshhg_masker.hpp"
#include "snap_table_builder.hpp"
#include "cch_preprocessor.hpp"
#include "graph_serialiser.hpp"
#include "maritime/edge_weight.hpp"
#include "maritime/weather_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace maritime::graph_builder {

namespace {

// Haversine reused here during graph construction (no StaticGraph yet)
float haversine_nm_local(float lat1, float lon1, float lat2, float lon2) noexcept
{
    constexpr float R_NM  = 3440.065f;
    constexpr float DEG2R = std::numbers::pi_v<float> / 180.f;
    const float dlat = (lat2 - lat1) * DEG2R;
    const float dlon = (lon2 - lon1) * DEG2R;
    const float rl1  = lat1 * DEG2R;
    const float rl2  = lat2 * DEG2R;
    const float a    = std::sin(dlat*.5f)*std::sin(dlat*.5f)
                     + std::cos(rl1)*std::cos(rl2)
                     * std::sin(dlon*.5f)*std::sin(dlon*.5f);
    return 2.f * R_NM * std::asin(std::sqrt(a));
}

struct Stopwatch {
    using Clock = std::chrono::steady_clock;
    Clock::time_point t0 = Clock::now();
    double elapsed_s() const {
        return std::chrono::duration<double>(Clock::now() - t0).count();
    }
};

} // namespace

// ---------------------------------------------------------------------------
// run() — full offline pipeline
// ---------------------------------------------------------------------------
void run(const BuildConfig& cfg)
{
    namespace fs = std::filesystem;

    fs::create_directories(cfg.output_dir);

    const std::string graph_out     = cfg.output_dir + "/graph.bin";
    const std::string flags_out     = cfg.output_dir + "/flags.bin";
    const std::string snap_wave_out = cfg.output_dir + "/snap_wave.bin";
    const std::string snap_wind_out = cfg.output_dir + "/snap_wind.bin";
    const std::string cch_out       = cfg.output_dir + "/cch_topo.bin";

    // ------------------------------------------------------------------
    // Step 1: Load GEBCO bathymetry and GSHHG land mask
    // ------------------------------------------------------------------
    Stopwatch sw;
    std::cout << "[1/6] Loading GEBCO bathymetry from " << cfg.gebco_path << "...\n";
    GebcoLoader gebco(cfg.gebco_path);
    std::cout << "      done in " << sw.elapsed_s() << "s\n";

    sw = {};
    std::cout << "[2/6] Loading GSHHG land mask from " << cfg.gshhg_path << "...\n";
    GshhgMasker gshhg(cfg.gshhg_path);
    std::cout << "      done in " << sw.elapsed_s() << "s\n";

    // ------------------------------------------------------------------
    // Step 2: Build global grid, prune land and shallow nodes
    // ------------------------------------------------------------------
    sw = {};
    std::cout << "[3/6] Building ocean graph at " << cfg.resolution_deg << "° resolution...\n";
    if (cfg.restricted_zones.empty()) {
        std::cout << "      Restricted zones: none\n";
    } else {
        for (const auto& z : cfg.restricted_zones)
            std::cout << "      Restricted zone : " << z.name
                      << " [" << z.lat_min << "°N–" << z.lat_max << "°N, "
                      << z.lon_min << "°–" << z.lon_max << "°]\n";
    }

    const float res  = cfg.resolution_deg;
    const int   n_lat = static_cast<int>(180.f / res) + 1;  // 721 at 0.25°
    const int   n_lon = static_cast<int>(360.f / res);      // 1440 at 0.25°

    // node_id[lat_i * n_lon + lon_i] = graph node index, or UINT32_MAX if pruned
    std::vector<uint32_t> node_id(
        static_cast<std::size_t>(n_lat) * n_lon, UINT32_MAX);

    GraphData data;

    for (int ri = 0; ri < n_lat; ++ri) {
        const float lat = 90.f - ri * res;
        for (int ci = 0; ci < n_lon; ++ci) {
            const float lon_360 = ci * res;
            const float lon     = (lon_360 > 180.f) ? lon_360 - 360.f : lon_360;

            if (gshhg.is_land(lat, lon)) continue;

            const float depth = gebco.depth_at(lat, lon);
            if (depth < cfg.min_draft_m + 1.5f) continue;  // UKC headroom

            const uint32_t nid = static_cast<uint32_t>(data.lat.size());
            node_id[static_cast<std::size_t>(ri) * n_lon + ci] = nid;

            data.lat.push_back(lat);
            data.lon.push_back(lon);

            // Encode depth as float16 (clamp to uint16 range)
            uint16_t depth_f16 = 0;
            {
                // Simple float→float16 via union (portable on GCC/Clang with -march=native)
                _Float16 d16 = static_cast<_Float16>(std::min(depth, 65504.f));
                std::memcpy(&depth_f16, &d16, sizeof(uint16_t));
            }
            data.depth.push_back(depth_f16);
            data.flags.push_back(0u);

            // Apply geographic passage restrictions
            for (const auto& zone : cfg.restricted_zones) {
                if (lat >= zone.lat_min && lat <= zone.lat_max &&
                    lon >= zone.lon_min && lon <= zone.lon_max) {
                    data.flags.back() |= FLAG_RESTRICTED;
                    break;
                }
            }
        }
    }

    const uint32_t n_ocean_nodes = data.n_nodes();
    const uint32_t n_restricted = static_cast<uint32_t>(
        std::count(data.flags.begin(), data.flags.end(),
                   static_cast<uint8_t>(FLAG_RESTRICTED)));
    std::cout << "      " << n_ocean_nodes << " ocean nodes retained ("
              << n_restricted << " restricted)\n";
    std::cout << "      done in " << sw.elapsed_s() << "s\n";

    const auto canal_ids = inject_canal_nodes(data);
    std::cout << "      Injected " << canal_ids.size()
              << " canal waypoint nodes\n";

    const uint32_t n_nodes = data.n_nodes();

    // ------------------------------------------------------------------
    // Step 3: Build CSR edges — 8-connected neighbours
    // ------------------------------------------------------------------
    sw = {};
    std::cout << "[4/6] Building CSR edge arrays...\n";

    data.row_ptr.resize(n_nodes + 1, 0u);

    // First pass: count edges per node
    std::vector<std::vector<uint32_t>> adj(n_nodes);

    constexpr int DR8[8] = {-1,-1,-1, 0, 0, 1, 1, 1};
    constexpr int DC8[8] = {-1, 0, 1,-1, 1,-1, 0, 1};

    for (int ri = 0; ri < n_lat; ++ri) {
        for (int ci = 0; ci < n_lon; ++ci) {
            const uint32_t u = node_id[static_cast<std::size_t>(ri)*n_lon + ci];
            if (u == UINT32_MAX) continue;

            for (int d = 0; d < 8; ++d) {
                const int nr = ri + DR8[d];
                const int nc = (ci + DC8[d] + n_lon) % n_lon;
                if (nr < 0 || nr >= n_lat) continue;

                const uint32_t v =
                    node_id[static_cast<std::size_t>(nr)*n_lon + nc];
                if (v == UINT32_MAX) continue;

                adj[u].push_back(v);
            }
        }
    }

    add_canal_edges_to_adj(adj, data, canal_ids, n_ocean_nodes);

    // Second pass: flatten into CSR
    uint32_t edge_count = 0;
    for (uint32_t u = 0; u < n_nodes; ++u) {
        data.row_ptr[u] = edge_count;
        for (uint32_t v : adj[u]) {
            data.col_idx.push_back(v);
            data.base_dist_nm.push_back(
                haversine_nm_local(
                    data.lat[u], data.lon[u],
                    data.lat[v], data.lon[v]));
            ++edge_count;
        }
    }
    data.row_ptr[n_nodes] = edge_count;
    adj.clear();

    std::cout << "      " << edge_count << " edges\n";
    std::cout << "      done in " << sw.elapsed_s() << "s\n";

    // ------------------------------------------------------------------
    // Step 4: Build snap tables from sigwh.npy / was.npy NaN masks
    // ------------------------------------------------------------------
    sw = {};
    std::cout << "[5/6] Building wave snap table from " << cfg.sigwh_npy_path << "...\n";
    const SnapTable snap_wave = build_snap_table(
        cfg.sigwh_npy_path, maritime::WAVE_NJ, maritime::WX_NI);
    serialise_snap_table(snap_wave, maritime::WAVE_NJ, maritime::WX_NI, snap_wave_out);
    std::cout << "      written: " << snap_wave_out << "\n";

    std::cout << "      Building wind snap table from " << cfg.was_npy_path << "...\n";
    const SnapTable snap_wind = build_snap_table(
        cfg.was_npy_path, maritime::WIND_NJ, maritime::WX_NI);
    serialise_snap_table(snap_wind, maritime::WIND_NJ, maritime::WX_NI, snap_wind_out);
    std::cout << "      written: " << snap_wind_out << "\n";
    std::cout << "      done in " << sw.elapsed_s() << "s\n";

    // ------------------------------------------------------------------
    // Step 5: Serialise graph.bin and flags.bin
    // ------------------------------------------------------------------
    sw = {};
    std::cout << "[6/6] Serialising graph artifacts...\n";
    serialise_graph(data, graph_out, flags_out);
    std::cout << "      written: " << graph_out << "\n";
    std::cout << "      written: " << flags_out << "\n";

    // ------------------------------------------------------------------
    // Step 6: CCH topology preprocessing
    // ------------------------------------------------------------------
    std::cout << "      Building CCH topology (this may take several minutes)...\n";
    {
        // Reconstruct tail array for RoutingKit
        std::vector<uint32_t> tail(data.n_edges());
        for (uint32_t u = 0; u < n_nodes; ++u) {
            for (uint32_t e = data.row_ptr[u]; e < data.row_ptr[u+1]; ++e)
                tail[e] = u;
        }
        CchPreprocessor cch(n_nodes, tail, data.col_idx, data.lat, data.lon);
        cch.save(cch_out);
        std::cout << "      written: " << cch_out << "\n";
    }
    std::cout << "      done in " << sw.elapsed_s() << "s\n";

    std::cout << "\nBuild complete. Artifacts in: " << cfg.output_dir << "\n";
}

} // namespace maritime::graph_builder
