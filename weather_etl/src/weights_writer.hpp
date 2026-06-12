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

    static void write(
        const std::vector<uint32_t>& weights,
        int64_t base_epoch,
        const std::string& out_path);
};

} // namespace maritime::weather_etl
