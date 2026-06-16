#pragma once

#include "maritime/weights_header.hpp"
#include "maritime/static_graph.hpp"
#include "maritime/weather_manager.hpp"
#include "maritime/edge_weight.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::weather_etl {

class WeightsWriter {
public:
    // vessel == nullptr → legacy proxy (base_nm × weather factor), vessel-agnostic.
    // vessel != nullptr → speed-loss time cost (dist / actual_spd); CCH then optimises
    //                     the same metric used by compute_edge_cost final accumulation.
    [[nodiscard]] static std::vector<uint32_t> compute(
        const maritime::StaticGraph&  graph,
        const maritime::WeatherBuffer& wx,
        int                           ref_time_step,
        const maritime::VesselParams* vessel = nullptr);

    // Temporal Gaussian blending: integrates cost over all 24 forecast
    // timesteps, weighting each by the probability the vessel is at node u
    // at that time (anisotropic Gaussian in along-track / cross-track frame).
    //
    //   w(u,v) = Σ_ts  g(u,ts) × cost(u,v,ts)  /  Σ_ts g(u,ts)
    //
    //   g(u,ts) = exp(-½ [(d_along/σ_along)² + (d_perp/σ_perp)²])
    //   d_along = haversine(origin, u) − ts × service_speed_kts   [nm]
    //   d_perp  = |cross_track_nm(origin→dest great circle, u)|   [nm]
    //
    // sigma_along: timing spread (default 100 nm ≈ 7 h at 14 kts)
    // sigma_perp:  path-choice spread (default 300 nm ≈ ±5° latitude)
    [[nodiscard]] static std::vector<uint32_t> compute_blended(
        const maritime::StaticGraph&   graph,
        const maritime::WeatherBuffer& wx,
        const maritime::VesselParams&  vessel,
        float origin_lat,  float origin_lon,
        float dest_lat,    float dest_lon,
        float sigma_along_nm = 100.f,
        float sigma_perp_nm  = 300.f);

    static void write(
        const std::vector<uint32_t>& weights,
        int64_t base_epoch,
        const std::string& out_path);
};

} // namespace maritime::weather_etl
