#pragma once

#include <string>
#include <vector>

namespace maritime::graph_builder {

// Loads a GEBCO ("elevation") or ETOPO ("z") NetCDF bathymetry file and
// exposes depth lookup at 0.25° resolution.
//
// Depth convention: positive value = metres below sea level (deeper = larger).
// Land cells return 0.  The constructor subsamples the source grid to the
// 0.25° graph grid (721 × 1440) at load time; queries are O(1) array reads.
class GebcoLoader {
public:
    explicit GebcoLoader(const std::string& path);

    // Returns ocean depth in metres (positive = deeper). Returns 0 for land.
    // lon expected in −180..+180 convention (graph_builder.cpp convention).
    [[nodiscard]] float depth_at(float lat, float lon) const noexcept;

private:
    // Pre-sampled depth grid at 0.25°.
    // Row 0 = 90°N, row 720 = 90°S.  Col 0 = −180°W, col 1439 = +179.75°E.
    std::vector<float> depth_grid_;
};

} // namespace maritime::graph_builder
