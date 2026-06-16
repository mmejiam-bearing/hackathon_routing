#include "graph_builder/src/graph_serialiser.hpp"
#include "maritime/static_graph.hpp"
#include "maritime/edge_weight.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Integration tests — exercise the full graph_builder → StaticGraph round trip.
//
// These tests build a tiny synthetic graph in memory, serialise it to disk
// using graph_serialiser, then load it back with StaticGraph (mmap) and
// verify all fields survive the round trip intact.
//
// No external data files are required — the graph is fabricated in the test.
// ---------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Build a tiny 4-node, 4-edge graph for testing:
//
//   0 ------- 1
//   |         |
//   2 ------- 3
//
// lat/lon chosen to give known haversine distances.
// ---------------------------------------------------------------------------
maritime::graph_builder::GraphData make_test_graph()
{
    maritime::graph_builder::GraphData g;

    // Nodes
    g.lat   = {51.5f, 51.5f, 50.5f, 50.5f};
    g.lon   = {-1.0f,  1.0f, -1.0f,  1.0f};

    // float16 depths — all 20m
    _Float16 d20 = static_cast<_Float16>(20.f);
    uint16_t d20_bits; std::memcpy(&d20_bits, &d20, 2);
    g.depth = {d20_bits, d20_bits, d20_bits, d20_bits};

    // Flags — node 1 is ECA, node 3 is canal transit
    g.flags = {0x00, maritime::FLAG_ECA, 0x00, maritime::FLAG_CANAL_TRANSIT};

    // CSR edges: 0→1, 1→3, 3→2, 2→0
    g.row_ptr      = {0, 1, 2, 3, 4};
    g.col_idx      = {1, 3, 2, 0};
    g.base_dist_nm = {
        maritime::haversine_nm(51.5f, -1.f, 51.5f,  1.f),  // 0→1
        maritime::haversine_nm(51.5f,  1.f, 50.5f,  1.f),  // 1→3
        maritime::haversine_nm(50.5f,  1.f, 50.5f, -1.f),  // 3→2
        maritime::haversine_nm(50.5f, -1.f, 51.5f, -1.f),  // 2→0
    };

    return g;
}

// Write a minimal all-identity snap table for the given grid shape.
static void write_minimal_snap(const std::string& path, int nj, int ni)
{
    maritime::SnapHeader hdr{};
    hdr.magic   = 0x5041'4E53u;   // "SNAP" LE
    hdr.version = 1;
    hdr.n_lat   = static_cast<uint32_t>(nj);
    hdr.n_lon   = static_cast<uint32_t>(ni);

    const std::size_t n = static_cast<std::size_t>(nj) * static_cast<std::size_t>(ni);
    std::vector<uint16_t> snap_lat(n), snap_lon(n);
    for (std::size_t i = 0; i < n; ++i) {
        snap_lat[i] = static_cast<uint16_t>(i / static_cast<std::size_t>(ni));
        snap_lon[i] = static_cast<uint16_t>(i % static_cast<std::size_t>(ni));
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(snap_lat.data()), n * 2);
    f.write(reinterpret_cast<const char*>(snap_lon.data()), n * 2);
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class GraphRoundTripTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmp_ = fs::temp_directory_path() / "maritime_graph_rt_test";
        fs::create_directories(tmp_);
        graph_path_     = (tmp_ / "graph.bin").string();
        flags_path_     = (tmp_ / "flags.bin").string();
        snap_wave_path_ = (tmp_ / "snap_wave.bin").string();
        snap_wind_path_ = (tmp_ / "snap_wind.bin").string();
    }
    void TearDown() override { fs::remove_all(tmp_); }

    void write_minimal_snaps()
    {
        write_minimal_snap(snap_wave_path_, maritime::WAVE_NJ, maritime::WX_NI);
        write_minimal_snap(snap_wind_path_, maritime::WIND_NJ, maritime::WX_NI);
    }

    fs::path    tmp_;
    std::string graph_path_;
    std::string flags_path_;
    std::string snap_wave_path_;
    std::string snap_wind_path_;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(GraphRoundTripTest, HeaderMagicAndCounts)
{
    auto data = make_test_graph();
    maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
    write_minimal_snaps();

    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);

    EXPECT_EQ(graph.n_nodes(), 4u);
    EXPECT_EQ(graph.n_edges(), 4u);
}

TEST_F(GraphRoundTripTest, NodeCoordinates)
{
    auto data = make_test_graph();
    maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
    write_minimal_snaps();

    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);

    ASSERT_EQ(graph.lat().size(), 4u);
    EXPECT_FLOAT_EQ(graph.lat()[0], 51.5f);
    EXPECT_FLOAT_EQ(graph.lat()[2], 50.5f);
    EXPECT_FLOAT_EQ(graph.lon()[1],  1.0f);
    EXPECT_FLOAT_EQ(graph.lon()[2], -1.0f);
}

TEST_F(GraphRoundTripTest, DepthValuesRoundTrip)
{
    auto data = make_test_graph();
    maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
    write_minimal_snaps();

    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);

    for (uint32_t n = 0; n < 4; ++n) {
        const float d = static_cast<float>(
            reinterpret_cast<const _Float16&>(graph.depth()[n]));
        EXPECT_NEAR(d, 20.f, 0.1f) << "node " << n;
    }
}

TEST_F(GraphRoundTripTest, FlagsRoundTrip)
{
    auto data = make_test_graph();
    maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
    write_minimal_snaps();

    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);

    EXPECT_EQ(graph.flags()[0], 0x00u);
    EXPECT_NE(graph.flags()[1] & maritime::FLAG_ECA,           0u);
    EXPECT_EQ(graph.flags()[2], 0x00u);
    EXPECT_NE(graph.flags()[3] & maritime::FLAG_CANAL_TRANSIT, 0u);
}

TEST_F(GraphRoundTripTest, CsrStructureRoundTrip)
{
    auto data = make_test_graph();
    maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
    write_minimal_snaps();

    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);

    // row_ptr[4] = 4 (total edges)
    ASSERT_EQ(graph.row_ptr().size(), 5u);
    EXPECT_EQ(graph.row_ptr()[4], 4u);

    // Edge 0→1: col_idx[0] = 1
    EXPECT_EQ(graph.col_idx()[0], 1u);
    // Edge 1→3: col_idx[1] = 3
    EXPECT_EQ(graph.col_idx()[1], 3u);
}

TEST_F(GraphRoundTripTest, BaseDistancesArePositive)
{
    auto data = make_test_graph();
    maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
    write_minimal_snaps();

    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);

    for (uint32_t e = 0; e < graph.n_edges(); ++e)
        EXPECT_GT(graph.base_dist()[e], 0.f) << "edge " << e;
}

TEST_F(GraphRoundTripTest, SnapTableIdentityForKnownOceanCell)
{
    auto data = make_test_graph();
    maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
    write_minimal_snaps();

    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);

    // Node 0: (51.5N, -1W). lon→360 = 359, lon_i=359/0.25=1436.
    // Wind grid (south-first, -90 base): lat_i=(51.5-(-90))/0.25=566
    const std::size_t idx_wind = graph.wind_weather_idx(0, 0);
    const std::size_t expected_wind = static_cast<std::size_t>(566) * 1440
                                     + static_cast<std::size_t>(1436);
    EXPECT_EQ(idx_wind, expected_wind);

    // Wave grid (south-first, -75 base): lat_i=(51.5-(-75))/0.25=506
    const std::size_t idx_wave = graph.wave_weather_idx(0, 0);
    const std::size_t expected_wave = static_cast<std::size_t>(506) * 1440
                                     + static_cast<std::size_t>(1436);
    EXPECT_EQ(idx_wave, expected_wave);
}

TEST_F(GraphRoundTripTest, ThrowsOnWrongMagic)
{
    // Write garbage to graph.bin
    { std::ofstream f(graph_path_); f << "not a graph file"; }
    write_minimal_snaps();
    { std::ofstream f(flags_path_); f << "x"; }

    EXPECT_THROW(
        maritime::StaticGraph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_),
        std::runtime_error);
}

} // namespace
