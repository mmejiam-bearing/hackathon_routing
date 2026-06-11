#pragma once

#include "maritime/weather_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::weather_etl {

// ---------------------------------------------------------------------------
// OcsLoader — loads ocean current speed+direction from the RTOFS grid
//
// S3 files: ocs.npy (speed m/s, float16) + ocd.npy (direction index, int8)
//
// Grid:  shape (1121, 2880)
//   lat: 70°N → 70°S at 0.125°  (row 0 = 70°N, row 1120 = 70°S)
//   lon: 0° → 359.875° at 0.125° (col 0 = 0°, col 2879 = 359.875°)
//
// Direction encoding: int8 value in [-1, 16]
//   -1  = land / missing (no current)
//    1  = N   (0°)
//    2  = NNE (22.5°)
//    ...
//   16  = NNW (337.5°)
//   angle_deg = (ocd_value - 1) * 22.5  where ocd_value ∈ [1, 16]
//
// Output:
//   Returns ocs_u (eastward) and ocs_v (northward) vectors of size
//   WX_N_POINTS = 1,038,240, resampled to the GFS (721 × 1440) grid by
//   nearest-neighbour lookup.  Cells outside ±70° lat receive 0.
// ---------------------------------------------------------------------------
class OcsLoader {
public:
    struct UVPair {
        std::vector<float> u;   // eastward component [m/s], size WX_N_POINTS
        std::vector<float> v;   // northward component [m/s], size WX_N_POINTS
    };

    // Load and regrid ocs.npy + ocd.npy to the GFS (721×1440) grid.
    [[nodiscard]] static UVPair load(
        const std::string& ocs_path,
        const std::string& ocd_path);
};

} // namespace maritime::weather_etl
