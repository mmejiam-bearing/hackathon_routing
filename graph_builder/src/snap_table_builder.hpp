#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::graph_builder {

struct SnapTable {
    std::vector<uint16_t> snap_lat;
    std::vector<uint16_t> snap_lon;
};

[[nodiscard]] SnapTable build_snap_table(const std::string& sigwh_npy_path);
void serialise_snap_table(const SnapTable& table, const std::string& out_path);

} // namespace maritime::graph_builder
