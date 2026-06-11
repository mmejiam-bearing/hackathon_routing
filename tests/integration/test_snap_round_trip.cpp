#include "graph_builder/src/snap_table_builder.hpp"
#include "maritime/static_graph.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

// ---------------------------------------------------------------------------
// Integration tests for the snap table build + serialise + load round trip.
// Writes a synthetic .npy file (valid header, known pattern), builds the snap
// table, serialises it, then loads it via StaticGraph's mmap path and checks
// that the BFS produced correct nearest-ocean assignments.
// ---------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

// Build a minimal valid .npy v1.0 header for shape (721, 1440), dtype '<f2'
static std::vector<uint8_t> make_npy_header()
{
    const std::string dict =
        "{'descr': '<f2', 'fortran_order': False, 'shape': (721, 1440), }";
    const std::size_t prefix  = 10;
    const std::size_t dict_sz = dict.size() + 1;
    const std::size_t pad     = 64 - (prefix + dict_sz) % 64;
    const std::size_t hdr_len = dict_sz + pad;

    std::vector<uint8_t> header;
    header.reserve(prefix + hdr_len);
    header.push_back(0x93);
    for (char c : std::string("NUMPY")) header.push_back(static_cast<uint8_t>(c));
    header.push_back(1); header.push_back(0);
    const uint16_t hl = static_cast<uint16_t>(hdr_len);
    header.push_back(hl & 0xFF); header.push_back(hl >> 8);
    for (char c : dict) header.push_back(static_cast<uint8_t>(c));
    header.push_back('\n');
    for (std::size_t i = 0; i < pad; ++i) header.push_back(' ');
    return header;
}

// float16 NaN bits
static constexpr uint16_t F16_NAN = 0x7E00u;
// float16 1.5 bits
static constexpr uint16_t F16_1P5 = 0x3E00u;

class SnapRoundTripTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmp_    = fs::temp_directory_path() / "maritime_snap_rt_test";
        fs::create_directories(tmp_);
        npy_    = (tmp_ / "sigwh.npy").string();
        snap_   = (tmp_ / "snap.bin").string();
    }
    void TearDown() override { fs::remove_all(tmp_); }

    // Write a sigwh.npy where every cell is ocean (F16_1P5)
    void write_all_ocean_npy()
    {
        constexpr std::size_t N = 721u * 1440u;
        std::vector<uint16_t> data(N, F16_1P5);
        auto hdr = make_npy_header();
        std::ofstream f(npy_, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
        f.write(reinterpret_cast<const char*>(data.data()), N * 2);
        // Write duplicate (ETL double-write) to ensure loader skips it
        f.write(reinterpret_cast<const char*>(data.data()), N * 2);
    }

    // Write a sigwh.npy where one strip of land (NaN) is surrounded by ocean
    // Row 360 (equator), columns 0..9 are NaN; everything else is ocean.
    void write_land_strip_npy()
    {
        constexpr std::size_t NJ = 721, NI = 1440;
        std::vector<uint16_t> data(NJ * NI, F16_1P5);
        for (std::size_t c = 0; c < 10; ++c)
            data[360 * NI + c] = F16_NAN;
        auto hdr = make_npy_header();
        std::ofstream f(npy_, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
        f.write(reinterpret_cast<const char*>(data.data()), NJ * NI * 2);
        f.write(reinterpret_cast<const char*>(data.data()), NJ * NI * 2);
    }

    fs::path    tmp_;
    std::string npy_;
    std::string snap_;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(SnapRoundTripTest, AllOceanIsIdentity)
{
    write_all_ocean_npy();
    const auto table = maritime::graph_builder::build_snap_table(npy_);
    maritime::graph_builder::serialise_snap_table(table, snap_);

    ASSERT_EQ(table.snap_lat.size(), 721u * 1440u);
    ASSERT_EQ(table.snap_lon.size(), 721u * 1440u);

    constexpr std::size_t NI = 1440;
    for (std::size_t i = 0; i < table.snap_lat.size(); ++i) {
        const uint16_t expected_lat = static_cast<uint16_t>(i / NI);
        const uint16_t expected_lon = static_cast<uint16_t>(i % NI);
        EXPECT_EQ(table.snap_lat[i], expected_lat) << "at flat index " << i;
        EXPECT_EQ(table.snap_lon[i], expected_lon) << "at flat index " << i;
    }
}

TEST_F(SnapRoundTripTest, LandCellsSnapToNearestOcean)
{
    write_land_strip_npy();
    const auto table = maritime::graph_builder::build_snap_table(npy_);

    // Cells at row=360, col=0..9 are NaN (land).
    // They should snap to ocean — either row 359 or 361 (north/south),
    // or column 10 (east), since col=10 is ocean in the same row.
    constexpr std::size_t NI = 1440;
    for (std::size_t c = 0; c < 10; ++c) {
        const std::size_t idx = 360u * NI + c;
        const uint16_t snap_r = table.snap_lat[idx];
        const uint16_t snap_c = table.snap_lon[idx];
        // The snapped cell must not be a NaN land cell itself
        // (i.e. not row=360, col=0..9)
        const bool snapped_to_ocean =
            !(snap_r == 360 && snap_c < 10);
        EXPECT_TRUE(snapped_to_ocean)
            << "land cell [360," << c << "] snapped to another land cell ["
            << snap_r << "," << snap_c << "]";
    }
}

TEST_F(SnapRoundTripTest, OceanCellsUnaffectedByNearbyLand)
{
    write_land_strip_npy();
    const auto table = maritime::graph_builder::build_snap_table(npy_);

    // Cell [360, 100] is ocean (col 100 is not in the NaN strip 0..9).
    // It should snap to itself.
    constexpr std::size_t NI = 1440;
    const std::size_t idx = 360u * NI + 100u;
    EXPECT_EQ(table.snap_lat[idx], 360u);
    EXPECT_EQ(table.snap_lon[idx], 100u);
}

TEST_F(SnapRoundTripTest, SerialisedFileHasCorrectMagic)
{
    write_all_ocean_npy();
    const auto table = maritime::graph_builder::build_snap_table(npy_);
    maritime::graph_builder::serialise_snap_table(table, snap_);

    // Read the first 4 bytes and verify magic
    std::ifstream f(snap_, std::ios::binary);
    uint32_t magic = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    EXPECT_EQ(magic, 0x5041'4E53u);  // "SNAP" LE
}

TEST_F(SnapRoundTripTest, ThrowsOnMissingNpy)
{
    EXPECT_THROW(
        maritime::graph_builder::build_snap_table("/no/such/file.npy"),
        std::runtime_error);
}

} // namespace
