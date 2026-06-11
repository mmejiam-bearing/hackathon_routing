#pragma once

#include <string>
#include <vector>

namespace maritime::graph_builder {

// Loads GSHHG high-resolution coastline polygons from a shapefile directory and
// exposes O(1) land/ocean classification at 0.25° resolution.
//
// The 144,749 GSHHS_h_L1.shp polygons are rasterized onto the 721×1440 graph
// grid at construction time (~1–2 s).  is_land() is then a plain array read.
class GshhgMasker {
public:
    explicit GshhgMasker(const std::string& path);

    // Returns true if the 0.25° cell containing (lat, lon) is land.
    // lon expected in −180..+180 convention.
    [[nodiscard]] bool is_land(float lat, float lon) const noexcept;

private:
    // Pre-rasterized land mask: 721 rows × 1440 cols.
    // Row 0 = 90°N, row 720 = 90°S.  Col 0 = −180°W, col 1439 = +179.75°E.
    std::vector<bool> land_mask_;
};

} // namespace maritime::graph_builder
