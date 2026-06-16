#pragma once

#include <string>
#include <vector>

namespace maritime::graph_builder {

// ---------------------------------------------------------------------------
// RestrictedZone
//
// Bounding-box zone in which nodes are flagged FLAG_RESTRICTED during build.
// Edges from restricted nodes return INFEASIBLE cost, so the router avoids
// them entirely — equivalent to setting edge weight to infinity.
// ---------------------------------------------------------------------------
struct RestrictedZone {
    const char* name;
    float lat_min, lat_max;   // degrees, north-positive
    float lon_min, lon_max;   // degrees, -180..180
};

// Default restriction set (mirrors searoute-py's behaviour):
//   arctic_polar_cap  — blocks transpolar shortcuts through 90°N
//   northwest_passage — Canadian Arctic Archipelago (ice-covered, not commercially viable)
inline const std::vector<RestrictedZone> DEFAULT_RESTRICTED_ZONES = {
    {"arctic_polar_cap",  75.f, 90.f, -180.f,  180.f},
    {"northwest_passage", 65.f, 75.f, -170.f,  -60.f},
};

// ---------------------------------------------------------------------------
// BuildConfig
//
// All inputs and tuning parameters for a single graph build run.
// Plain aggregate — Rule of Zero.
// ---------------------------------------------------------------------------
struct BuildConfig {
    std::string gebco_path;       // GEBCO 2023 NetCDF (.nc)
    std::string gshhg_path;       // GSHHG directory containing .shp files
    std::string enc_path;         // NOAA ENC directory containing .000 files
    std::string sigwh_npy_path;   // sigwh.npy — wave-grid land/ocean mask source
    std::string was_npy_path;     // was.npy — wind-grid land/ocean mask source
    std::string output_dir;       // destination for graph.bin, flags.bin, snap_*.bin

    float resolution_deg = 0.25f; // graph node spacing in degrees
    float min_draft_m    = 3.0f;  // shallowest vessel draft to support

    std::vector<RestrictedZone> restricted_zones = DEFAULT_RESTRICTED_ZONES;
};

// ---------------------------------------------------------------------------
// run()
//
// Executes the full offline pipeline in sequence:
//   1. Load GSHHG land polygons + GEBCO bathymetry
//   2. Build global grid, prune land and shallow nodes
//   3. Apply geographic passage restrictions (FLAG_RESTRICTED)
//   4. Build snap tables from sigwh.npy / was.npy NaN masks
//   5. Run CCH topology preprocessing via RoutingKit
//   6. Serialise graph.bin, flags.bin, snap_wave.bin, snap_wind.bin to output_dir
//
// Throws std::runtime_error on any failure.
// Idempotent: re-running overwrites previous output.
// ---------------------------------------------------------------------------
void run(const BuildConfig& cfg);

} // namespace maritime::graph_builder
