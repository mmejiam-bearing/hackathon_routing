#pragma once

#include "maritime/weights_header.hpp"
#include "maritime/static_graph.hpp"
#include "maritime/weather_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::weather_etl {

class WeightsWriter {
public:
    [[nodiscard]] static std::vector<uint32_t> compute(
        const maritime::StaticGraph& graph,
        const maritime::WeatherBuffer& wx,
        int ref_time_step);

    static void write(
        const std::vector<uint32_t>& weights,
        int64_t base_epoch,
        const std::string& out_path);
};

} // namespace maritime::weather_etl
