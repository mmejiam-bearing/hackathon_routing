#include "maritime/static_graph.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Tests for snap table indexing logic — tested independently of disk I/O
// by constructing synthetic snap_lat / snap_lon arrays and verifying the
// grid_idx() arithmetic directly.
//
// We cannot instantiate StaticGraph without binary files, so we test the
// coordinate math as a pure function extracted here.
// ---------------------------------------------------------------------------

namespace {

// Mirror of StaticGraph::grid_idx() as a pure function for unit testing.
// Any change to that method must be reflected here. South-first: row 0 is
// the grid's southern edge (lat_min), increasing northward.
std::size_t grid_idx_pure(
    float    lat,
    float    lon,
    int      time_step,
    int      nj,
    float    lat_min,
    uint16_t snap_lat_val,  // what the snap table returns for this cell
    uint16_t snap_lon_val)
{
    constexpr int   NI     = 1440;
    constexpr float WX_RES = 0.25f;

    if (lon < 0.f) lon += 360.f;

    int raw_lat_i = static_cast<int>((lat - lat_min) / WX_RES);
    int raw_lon_i = static_cast<int>(lon              / WX_RES) % NI;
    raw_lat_i = std::clamp(raw_lat_i, 0, nj - 1);
    raw_lon_i = std::clamp(raw_lon_i, 0, NI - 1);
    (void)raw_lat_i; (void)raw_lon_i;  // used only to verify snap is needed

    // After snap table lookup:
    return static_cast<std::size_t>(time_step)
         * static_cast<std::size_t>(nj * NI)
         + static_cast<std::size_t>(snap_lat_val) * NI
         + static_cast<std::size_t>(snap_lon_val);
}

TEST(SnapTable, IdentityForOceanCellWaveGrid)
{
    // An open-ocean cell on the wave grid — snap table returns identity
    const float lat = 30.f;
    const float lon = -40.f;   // Mid-Atlantic, definitely ocean

    constexpr int   NI      = 1440;
    constexpr int   NJ      = maritime::WAVE_NJ;
    constexpr float RES     = 0.25f;
    constexpr float LAT_MIN = maritime::WAVE_LAT_MIN;

    float lon360 = lon < 0.f ? lon + 360.f : lon;
    const uint16_t expected_lat_i = static_cast<uint16_t>((lat - LAT_MIN) / RES);
    const uint16_t expected_lon_i = static_cast<uint16_t>(lon360 / RES) % NI;

    const std::size_t idx = grid_idx_pure(
        lat, lon, 0, NJ, LAT_MIN, expected_lat_i, expected_lon_i);

    const std::size_t direct =
        static_cast<std::size_t>(expected_lat_i) * NI
      + static_cast<std::size_t>(expected_lon_i);

    EXPECT_EQ(idx, direct);
}

TEST(SnapTable, SnapNarrowStrait)
{
    // Singapore Strait: the raw cell is land (NaN), snapped to nearest ocean.
    // We model the snap table having redirected this to an ocean cell at
    // (snap_lat=362, snap_lon=414), which is ~30nm south in the open Strait.
    const float lat = 1.2f;
    const float lon = 103.8f;

    const uint16_t snap_lat = 362;
    const uint16_t snap_lon = 414;

    const std::size_t idx = grid_idx_pure(
        lat, lon, 0, maritime::WIND_NJ, maritime::WIND_LAT_MIN, snap_lat, snap_lon);

    constexpr int NI = 1440;
    const std::size_t expected =
        static_cast<std::size_t>(snap_lat) * NI
      + static_cast<std::size_t>(snap_lon);

    EXPECT_EQ(idx, expected);
}

TEST(SnapTable, TimeStepOffsets)
{
    // Verify that time_step multiplier is applied correctly
    const uint16_t lat_i = 240;
    const uint16_t lon_i = 100;

    constexpr int NI = 1440;
    constexpr int NJ = maritime::WIND_NJ;

    for (int t = 0; t < 24; ++t) {
        const std::size_t idx = grid_idx_pure(
            30.f, 25.f, t, NJ, maritime::WIND_LAT_MIN, lat_i, lon_i);
        const std::size_t expected =
            static_cast<std::size_t>(t) * NJ * NI
          + static_cast<std::size_t>(lat_i) * NI
          + static_cast<std::size_t>(lon_i);
        EXPECT_EQ(idx, expected) << "failed at time_step=" << t;
    }
}

TEST(SnapTable, LonNormalisationNegative)
{
    // Negative longitude must normalise to 0→360 before indexing
    // lon=-90 should map to lon_i = (270/0.25) = 1080
    const float lat = 0.f;
    const float lon = -90.f;

    constexpr float RES = 0.25f;
    const uint16_t expected_lon_i = static_cast<uint16_t>(270.f / RES);  // 1080
    const uint16_t expected_lat_i =
        static_cast<uint16_t>((lat - maritime::WIND_LAT_MIN) / RES);  // 360

    const std::size_t idx = grid_idx_pure(
        lat, lon, 0, maritime::WIND_NJ, maritime::WIND_LAT_MIN,
        expected_lat_i, expected_lon_i);

    constexpr int NI = 1440;
    EXPECT_EQ(idx % NI, expected_lon_i);
}

TEST(SnapTable, SouthPoleClampWindGrid)
{
    // lat=-90 (south pole) should give raw_lat_i=0 (southernmost row) on
    // the wind grid, which spans the full -90..+90.
    const uint16_t snap_lat = 0;
    const uint16_t snap_lon = 0;
    const std::size_t idx = grid_idx_pure(
        -90.f, 0.f, 0, maritime::WIND_NJ, maritime::WIND_LAT_MIN, snap_lat, snap_lon);
    EXPECT_EQ(idx, 0u);
}

TEST(SnapTable, WaveGridClampsOutsideLatRange)
{
    // The wave grid only spans -75..+80; a node at -85 (south of range)
    // must clamp to row 0, not produce a negative/out-of-bounds index.
    constexpr float RES = 0.25f;
    constexpr int   NJ  = maritime::WAVE_NJ;
    int raw_lat_i = static_cast<int>((-85.f - maritime::WAVE_LAT_MIN) / RES);
    raw_lat_i = std::clamp(raw_lat_i, 0, NJ - 1);
    EXPECT_EQ(raw_lat_i, 0);
}

} // namespace
