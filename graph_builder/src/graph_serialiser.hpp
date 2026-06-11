#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::graph_builder {

struct GraphData {
    std::vector<float> lat;
    std::vector<float> lon;
    std::vector<uint16_t> depth;
    std::vector<uint32_t> row_ptr;
    std::vector<uint32_t> col_idx;
    std::vector<float> base_dist_nm;
    std::vector<uint8_t> flags;

    [[nodiscard]] uint32_t n_nodes() const noexcept {
        return static_cast<uint32_t>(lat.size());
    }

    [[nodiscard]] uint32_t n_edges() const noexcept {
        return static_cast<uint32_t>(col_idx.size());
    }
};

void serialise_graph(
    const GraphData& data,
    const std::string& graph_out_path,
    const std::string& flags_out_path);

} // namespace maritime::graph_builder
