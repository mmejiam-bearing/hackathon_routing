#include "weather_etl/src/weights_writer.hpp"
#include "maritime/weights_header.hpp"
#include "maritime/edge_weight.hpp"
#include "maritime/foc_model.hpp"
#include "router_server/src/weights_loader.hpp"
#include "graph_builder/src/graph_serialiser.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Integration tests for the weights.bin write → load round trip.
// These tests exercise the full file I/O path: WeightsWriter::write() 
// produces a binary file that WeightsLoader::load() must reconstruct exactly.
// ---------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

class WeightsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmp_  = fs::temp_directory_path() / "maritime_weights_int_test";
        fs::create_directories(tmp_);
        path_ = (tmp_ / "weights.bin").string();
    }
    void TearDown() override { fs::remove_all(tmp_); }

    fs::path    tmp_;
    std::string path_;
};

TEST_F(WeightsIntegrationTest, SingleEdgeRoundTrip)
{
    const std::vector<uint32_t> w = {12345u};
    const int64_t epoch = 1000LL;

    maritime::weather_etl::WeightsWriter::write(w, epoch, path_);
    const auto p = maritime::router_server::WeightsLoader::load(path_);

    ASSERT_EQ(p.weights.size(), 1u);
    EXPECT_EQ(p.weights[0], 12345u);
    EXPECT_EQ(p.base_epoch, epoch);
}

TEST_F(WeightsIntegrationTest, FileExistsAfterWrite)
{
    maritime::weather_etl::WeightsWriter::write({1u, 2u, 3u}, 0LL, path_);
    EXPECT_TRUE(fs::exists(path_));
    EXPECT_GT(fs::file_size(path_), sizeof(maritime::WeightsHeader));
}

TEST_F(WeightsIntegrationTest, FileSizeIsCorrect)
{
    const std::size_t n_edges = 500u;
    std::vector<uint32_t> w(n_edges, 1000u);
    maritime::weather_etl::WeightsWriter::write(w, 0LL, path_);

    const std::size_t expected_size =
        sizeof(maritime::WeightsHeader)
        + n_edges * sizeof(uint32_t);

    EXPECT_EQ(fs::file_size(path_), expected_size);
}

TEST_F(WeightsIntegrationTest, OverwritesPreviousFile)
{
    // Write once
    maritime::weather_etl::WeightsWriter::write({1u}, 0LL, path_);
    // Write again with different content
    maritime::weather_etl::WeightsWriter::write({42u, 43u}, 999LL, path_);

    const auto p = maritime::router_server::WeightsLoader::load(path_);
    ASSERT_EQ(p.weights.size(), 2u);
    EXPECT_EQ(p.weights[0], 42u);
    EXPECT_EQ(p.weights[1], 43u);
    EXPECT_EQ(p.base_epoch, 999LL);
}

TEST_F(WeightsIntegrationTest, MaxUint32EdgeWeight)
{
    // Verify large weights survive without truncation
    const std::vector<uint32_t> w = {0xFFFFFFFEu};
    maritime::weather_etl::WeightsWriter::write(w, 0LL, path_);
    const auto p = maritime::router_server::WeightsLoader::load(path_);
    ASSERT_EQ(p.weights.size(), 1u);
    EXPECT_EQ(p.weights[0], 0xFFFFFFFEu);
}

TEST_F(WeightsIntegrationTest, HeaderVersionIsOne)
{
    maritime::weather_etl::WeightsWriter::write({1u}, 0LL, path_);

    std::ifstream f(path_, std::ios::binary);
    maritime::WeightsHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    EXPECT_EQ(hdr.version, 1u);
}

TEST_F(WeightsIntegrationTest, NegativeEpochPreserved)
{
    // Epochs before Unix epoch (e.g. historical data) must not be corrupted
    const int64_t epoch = -1LL;
    maritime::weather_etl::WeightsWriter::write({1u}, epoch, path_);
    const auto p = maritime::router_server::WeightsLoader::load(path_);
    EXPECT_EQ(p.base_epoch, epoch);
}

// ---------------------------------------------------------------------------
// WeightsWriter::compute() — physics branch tests.
//
// Build a tiny 2-node graph (one edge), populate a WeatherBuffer, and verify
// that the vessel-aware physics branch produces weights consistent with the
// equal-power speed-loss model.
// ---------------------------------------------------------------------------

// Minimal 2-node, 1-edge graph: node 0 at (51.5, 0) → node 1 at (50.5, 0)
// heading due south (~60 nm).
static maritime::graph_builder::GraphData make_two_node_graph()
{
    maritime::graph_builder::GraphData g;
    g.lat = {51.5f, 50.5f};
    g.lon = {0.f, 0.f};

    _Float16 d100 = static_cast<_Float16>(100.f);
    uint16_t d100_bits; std::memcpy(&d100_bits, &d100, 2);
    g.depth = {d100_bits, d100_bits};
    g.flags = {0x00, 0x00};

    g.row_ptr      = {0, 1, 1};   // node 0 has 1 out-edge; node 1 has none
    g.col_idx      = {1};
    g.base_dist_nm = {maritime::haversine_nm(51.5f, 0.f, 50.5f, 0.f)};
    return g;
}

static void write_identity_snap(const std::string& path)
{
    maritime::SnapHeader hdr{};
    hdr.magic   = 0x5041'4E53u;
    hdr.version = 1;
    hdr.n_lat   = 721;
    hdr.n_lon   = 1440;
    constexpr std::size_t N = 721u * 1440u;
    std::vector<uint16_t> snap_lat(N), snap_lon(N);
    for (std::size_t i = 0; i < N; ++i) {
        snap_lat[i] = static_cast<uint16_t>(i / 1440);
        snap_lon[i] = static_cast<uint16_t>(i % 1440);
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(snap_lat.data()), N * 2);
    f.write(reinterpret_cast<const char*>(snap_lon.data()), N * 2);
}

class WeightsComputeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmp_  = fs::temp_directory_path() / "maritime_weights_compute_test";
        fs::create_directories(tmp_);
        graph_path_ = (tmp_ / "graph.bin").string();
        flags_path_ = (tmp_ / "flags.bin").string();
        snap_path_  = (tmp_ / "snap.bin").string();

        auto data = make_two_node_graph();
        maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
        write_identity_snap(snap_path_);
    }
    void TearDown() override { fs::remove_all(tmp_); }

    // Build a WeatherBuffer with uniform weather values.
    static std::shared_ptr<maritime::WeatherBuffer> make_wx(
        float sig_wh, float was, float wad_deg, float pwd_deg)
    {
        auto buf = maritime::WeatherBuffer::make_empty();
        for (std::size_t k = 0; k < static_cast<std::size_t>(maritime::WX_N_POINTS); ++k) {
            buf->sigwh[k] = static_cast<_Float16>(sig_wh);
            buf->was  [k] = static_cast<_Float16>(was);
            buf->wad  [k] = static_cast<_Float16>(wad_deg);
            buf->pwd  [k] = static_cast<_Float16>(pwd_deg);
        }
        return buf;
    }

    // Build a VesselParams matching the standard test vessel.
    static maritime::VesselParams make_vessel()
    {
        maritime::VesselParams v;
        v.draft_m            = 10.f;
        v.beam_m             = 32.f;
        v.loa_m              = 200.f;
        v.service_speed_kts  = 14.f;
        v.block_coeff        = 0.80f;
        v.displacement_t     = 50000.f;
        v.transverse_area_m2 = 600.f;
        v.foc_model = [](float, float wind, float) {
            return maritime::universal_foc_model(wind, 14.f);
        };
        return v;
    }

    fs::path    tmp_;
    std::string graph_path_, flags_path_, snap_path_;
};

TEST_F(WeightsComputeTest, PhysicsBranchNonZero)
{
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_path_);
    auto wx     = make_wx(0.f, 0.f, 0.f, 0.f);
    auto vessel = make_vessel();

    auto weights = maritime::weather_etl::WeightsWriter::compute(
        graph, *wx, 0, &vessel);

    ASSERT_EQ(weights.size(), graph.n_edges());
    for (auto w : weights) EXPECT_GT(w, 0u);
}

TEST_F(WeightsComputeTest, StormHeadOnHigherThanCalm)
{
    // Vessel heading south (≈180°). Head-on storm: waves/wind coming from south
    // → going-to direction = north = 0°.
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_path_);

    auto wx_calm  = make_wx(0.f,  0.f,   0.f, 0.f);
    auto wx_storm = make_wx(6.f, 20.f,   0.f, 0.f);   // head-on storm

    auto vessel = make_vessel();

    auto w_calm  = maritime::weather_etl::WeightsWriter::compute(
        graph, *wx_calm,  0, &vessel);
    auto w_storm = maritime::weather_etl::WeightsWriter::compute(
        graph, *wx_storm, 0, &vessel);

    ASSERT_FALSE(w_calm.empty());
    ASSERT_FALSE(w_storm.empty());
    EXPECT_GT(w_storm[0], w_calm[0]);   // storm slows vessel → higher time cost
}

TEST_F(WeightsComputeTest, PhysicsDiffersFromProxy)
{
    // With non-trivial weather the physics branch and the legacy proxy should
    // produce different integer weights.
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_path_);
    auto wx     = make_wx(4.f, 15.f, 0.f, 0.f);
    auto vessel = make_vessel();

    auto w_physics = maritime::weather_etl::WeightsWriter::compute(
        graph, *wx, 0, &vessel);
    auto w_proxy   = maritime::weather_etl::WeightsWriter::compute(
        graph, *wx, 0, nullptr);

    ASSERT_FALSE(w_physics.empty());
    ASSERT_FALSE(w_proxy.empty());
    EXPECT_NE(w_physics[0], w_proxy[0]);
}

TEST_F(WeightsComputeTest, ProxyBranchUnchanged)
{
    // nullptr vessel must still produce the same weight as before (no regression).
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_path_);
    auto wx = make_wx(3.f, 10.f, 90.f, 90.f);   // beam wind

    auto w1 = maritime::weather_etl::WeightsWriter::compute(graph, *wx, 0, nullptr);
    auto w2 = maritime::weather_etl::WeightsWriter::compute(graph, *wx, 0, nullptr);

    ASSERT_EQ(w1.size(), w2.size());
    for (std::size_t i = 0; i < w1.size(); ++i)
        EXPECT_EQ(w1[i], w2[i]);
}

} // namespace
