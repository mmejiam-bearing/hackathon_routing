#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::graph_builder {

struct SnapTable {
    std::vector<uint16_t> snap_lat;
    std::vector<uint16_t> snap_lon;
};

// Builds a snap table for one weather grid from a float64 .npy land/ocean
// mask (NaN = land, finite = ocean). nj x ni is the grid shape (south-first
// row order doesn't matter here — the BFS only cares about adjacency).
[[nodiscard]] SnapTable build_snap_table(
    const std::string& mask_npy_path, int nj, int ni);

void serialise_snap_table(
    const SnapTable& table, int nj, int ni, const std::string& out_path);

} // namespace maritime::graph_builder
