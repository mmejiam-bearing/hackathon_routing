#include "weather_etl/src/weights_writer.hpp"
#include "maritime/weights_header.hpp"
#include "maritime/edge_weight.hpp"
#include "maritime/foc_model.hpp"
#include "router_server/src/weights_loader.hpp"
#include "graph_builder/src/graph_serialiser.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <queue>
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

static void write_identity_snap(const std::string& path, int nj, int ni)
{
    maritime::SnapHeader hdr{};
    hdr.magic   = 0x5041'4E53u;
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

static void write_identity_snaps(const std::string& wave_path, const std::string& wind_path)
{
    write_identity_snap(wave_path, maritime::WAVE_NJ, maritime::WX_NI);
    write_identity_snap(wind_path, maritime::WIND_NJ, maritime::WX_NI);
}

class WeightsComputeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmp_  = fs::temp_directory_path() / "maritime_weights_compute_test";
        fs::create_directories(tmp_);
        graph_path_     = (tmp_ / "graph.bin").string();
        flags_path_     = (tmp_ / "flags.bin").string();
        snap_wave_path_ = (tmp_ / "snap_wave.bin").string();
        snap_wind_path_ = (tmp_ / "snap_wind.bin").string();

        auto data = make_two_node_graph();
        maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
        write_identity_snaps(snap_wave_path_, snap_wind_path_);
    }
    void TearDown() override { fs::remove_all(tmp_); }

    // Build a WeatherBuffer with uniform weather values.
    static std::shared_ptr<maritime::WeatherBuffer> make_wx(
        float sig_wh, float was, float wad_deg, float pwd_deg)
    {
        auto buf = maritime::WeatherBuffer::make_empty();
        for (std::size_t k = 0; k < static_cast<std::size_t>(maritime::WAVE_N_POINTS); ++k) {
            buf->sigwh[k] = static_cast<_Float16>(sig_wh);
            buf->pwd  [k] = static_cast<_Float16>(pwd_deg);
        }
        for (std::size_t k = 0; k < static_cast<std::size_t>(maritime::WIND_N_POINTS); ++k) {
            buf->was[k] = static_cast<_Float16>(was);
            buf->wad[k] = static_cast<_Float16>(wad_deg);
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
    std::string graph_path_, flags_path_, snap_wave_path_, snap_wind_path_;
};

TEST_F(WeightsComputeTest, PhysicsBranchNonZero)
{
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);
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
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);

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
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);
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
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);
    auto wx = make_wx(3.f, 10.f, 90.f, 90.f);   // beam wind

    auto w1 = maritime::weather_etl::WeightsWriter::compute(graph, *wx, 0, nullptr);
    auto w2 = maritime::weather_etl::WeightsWriter::compute(graph, *wx, 0, nullptr);

    ASSERT_EQ(w1.size(), w2.size());
    for (std::size_t i = 0; i < w1.size(); ++i)
        EXPECT_EQ(w1[i], w2[i]);
}

// ---------------------------------------------------------------------------
// TemporalBlending — compute_blended() integrates weather across 24 timesteps.
//
// Uses the WeightsComputeTest fixture (2-node graph, identity snap).
// A per-timestep weather helper fills one timestep with storm and the rest
// with calm, letting each test isolate one aspect of the Gaussian weighting.
// ---------------------------------------------------------------------------

// Fill all wave/wind-grid elements uniformly (all timesteps × all grid points).
static std::shared_ptr<maritime::WeatherBuffer> make_wx_uniform(
    float sig_wh, float was, float wad_deg, float pwd_deg)
{
    auto buf = maritime::WeatherBuffer::make_empty();
    const std::size_t wave_total = static_cast<std::size_t>(maritime::WAVE_N_TOTAL);
    const std::size_t wind_total = static_cast<std::size_t>(maritime::WIND_N_TOTAL);
    for (std::size_t k = 0; k < wave_total; ++k) {
        buf->sigwh[k] = static_cast<_Float16>(sig_wh);
        buf->pwd  [k] = static_cast<_Float16>(pwd_deg);
    }
    for (std::size_t k = 0; k < wind_total; ++k) {
        buf->was[k] = static_cast<_Float16>(was);
        buf->wad[k] = static_cast<_Float16>(wad_deg);
    }
    return buf;
}

// Fill with calm everywhere, then set one timestep's entire spatial grid to
// storm conditions. Layout: [ts * <grid>_N_POINTS + spatial_idx].
static std::shared_ptr<maritime::WeatherBuffer> make_wx_timestep_storm(
    float storm_sig_wh, float storm_was, float storm_wad, float storm_pwd,
    int storm_ts)
{
    auto buf = maritime::WeatherBuffer::make_empty();  // all zeros (calm)
    const std::size_t wave_n = static_cast<std::size_t>(maritime::WAVE_N_POINTS);
    const std::size_t wind_n = static_cast<std::size_t>(maritime::WIND_N_POINTS);
    for (std::size_t s = 0; s < wave_n; ++s) {
        const std::size_t idx = static_cast<std::size_t>(storm_ts) * wave_n + s;
        buf->sigwh[idx] = static_cast<_Float16>(storm_sig_wh);
        buf->pwd  [idx] = static_cast<_Float16>(storm_pwd);
    }
    for (std::size_t s = 0; s < wind_n; ++s) {
        const std::size_t idx = static_cast<std::size_t>(storm_ts) * wind_n + s;
        buf->was[idx] = static_cast<_Float16>(storm_was);
        buf->wad[idx] = static_cast<_Float16>(storm_wad);
    }
    return buf;
}

// If every timestep has identical weather, the Gaussian-weighted average must
// equal the single-snapshot compute() result. This is the mathematical identity
// that must hold before any per-timestep variation is meaningful.
TEST_F(WeightsComputeTest, UniformWeatherMatchesSingleSnapshot)
{
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);
    auto vessel = make_vessel();

    // Moderate storm, same in all timesteps.
    auto wx_ts  = make_wx_uniform(3.f, 10.f, 0.f, 0.f);
    // Legacy make_wx fills only ts=0 spatial slice (per-grid N_POINTS elements).
    auto wx_ts0 = make_wx(3.f, 10.f, 0.f, 0.f);

    const float origin_lat = graph.lat()[0], origin_lon = graph.lon()[0];
    const float dest_lat   = graph.lat()[1], dest_lon   = graph.lon()[1];

    auto w_blended  = maritime::weather_etl::WeightsWriter::compute_blended(
        graph, *wx_ts, vessel, origin_lat, origin_lon, dest_lat, dest_lon);
    auto w_snapshot = maritime::weather_etl::WeightsWriter::compute(
        graph, *wx_ts0, 0, &vessel);

    ASSERT_EQ(w_blended.size(), w_snapshot.size());
    // Allow a 1% tolerance due to floating-point accumulation over 24 terms.
    for (std::size_t i = 0; i < w_blended.size(); ++i) {
        EXPECT_NEAR(static_cast<double>(w_blended[i]),
                    static_cast<double>(w_snapshot[i]),
                    static_cast<double>(w_snapshot[i]) * 0.01)
            << "edge " << i;
    }
}

// A storm at ts=0 only should raise the blended weight for the origin-adjacent
// node (t_mean≈0, Gaussian peaks at ts=0) more than for a node far enough
// away that its Gaussian peak is around ts=24 and barely sees ts=0.
//
// We use the graph's node 0 at (51.5,0) as the "early node" — origin is set
// at (51.5,0) so d_along=0 at ts=0, giving g=1. The "late" scenario uses an
// origin placed ~336 nm away (14kts × 24h), so node 0 has d_along≈336 nm at
// ts=0 and the Gaussian weight is negligible there.
TEST_F(WeightsComputeTest, EarlyNodeWeightsEarlyTimesteps)
{
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);
    auto vessel = make_vessel();

    // Storm only at ts=0; all other timesteps calm.
    auto wx = make_wx_timestep_storm(15.f, 45.f, 0.f, 0.f, 0);

    const float dest_lat = graph.lat()[1], dest_lon = graph.lon()[1];

    // Origin at node 0 itself → d_along=0 at ts=0 → maximum Gaussian weight
    auto w_early = maritime::weather_etl::WeightsWriter::compute_blended(
        graph, *wx, vessel,
        graph.lat()[0], graph.lon()[0], dest_lat, dest_lon);

    // Origin shifted 336 nm north → node 0 has d_along≈336 nm at ts=0 → near-zero weight
    auto w_late = maritime::weather_etl::WeightsWriter::compute_blended(
        graph, *wx, vessel,
        graph.lat()[0] + 24.f, graph.lon()[0], dest_lat, dest_lon);

    ASSERT_FALSE(w_early.empty());
    ASSERT_FALSE(w_late.empty());
    // The early origin should see more storm penalty on the single edge.
    EXPECT_GT(w_early[0], w_late[0]);
}

// A storm placed only at ts=12 should penalise the edge from a midpoint node
// (t_mean≈12h, d_along≈0 at ts=12) more than an origin-adjacent node
// (t_mean≈0h, d_along≈168nm at ts=12 → heavily attenuated).
TEST_F(WeightsComputeTest, StormAtMidpointAffectsMidpointNode)
{
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);
    auto vessel = make_vessel();

    // Storm only at ts=12.
    auto wx = make_wx_timestep_storm(15.f, 45.f, 0.f, 0.f, 12);

    const float dest_lat = graph.lat()[1], dest_lon = graph.lon()[1];

    // Origin placed so node 0 is ~168 nm away (14 kts × 12 h) → peak at ts=12.
    // 168 nm / 60 nm per degree ≈ 2.8° north.
    const float midpoint_origin_lat = graph.lat()[0] + 2.8f;
    auto w_mid = maritime::weather_etl::WeightsWriter::compute_blended(
        graph, *wx, vessel,
        midpoint_origin_lat, graph.lon()[0], dest_lat, dest_lon);

    // Origin at node 0 itself → d_along=168 nm at ts=12 → attenuated
    auto w_orig = maritime::weather_etl::WeightsWriter::compute_blended(
        graph, *wx, vessel,
        graph.lat()[0], graph.lon()[0], dest_lat, dest_lon);

    ASSERT_FALSE(w_mid.empty());
    ASSERT_FALSE(w_orig.empty());
    EXPECT_GT(w_mid[0], w_orig[0]);
}

// All blended weights must be strictly positive regardless of weather.
// RoutingKit treats weight=0 as invalid; this guards the fallback path
// that fires when all 24 Gaussian values are below the 1e-6 threshold.
TEST_F(WeightsComputeTest, ComputeBlendedNonZero)
{
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);
    auto vessel = make_vessel();
    auto wx     = make_wx_uniform(0.f, 0.f, 0.f, 0.f);

    const float origin_lat = graph.lat()[0], origin_lon = graph.lon()[0];
    const float dest_lat   = graph.lat()[1], dest_lon   = graph.lon()[1];

    auto weights = maritime::weather_etl::WeightsWriter::compute_blended(
        graph, *wx, vessel, origin_lat, origin_lon, dest_lat, dest_lon);

    ASSERT_EQ(weights.size(), graph.n_edges());
    for (auto w : weights) EXPECT_GT(w, 0u);
}

// With sig_wh=6m, 9m, 12m (all above the 4m threshold), the blended weight
// must grow faster than proportionally to sig_wh — the exponential multiplier
// should make w(12m) > 4× w(6m) (a linear model would give exactly 2×).
TEST_F(WeightsComputeTest, ExponentialPenaltySuperlinear)
{
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);
    auto vessel = make_vessel();

    const float origin_lat = graph.lat()[0], origin_lon = graph.lon()[0];
    const float dest_lat   = graph.lat()[1], dest_lon   = graph.lon()[1];

    auto run = [&](float wh) {
        auto wx = make_wx_uniform(wh, 0.f, 0.f, 0.f);
        auto w  = maritime::weather_etl::WeightsWriter::compute_blended(
            graph, *wx, vessel, origin_lat, origin_lon, dest_lat, dest_lon);
        EXPECT_FALSE(w.empty());
        return static_cast<double>(w[0]);
    };

    const double w6  = run(6.f);
    const double w12 = run(12.f);

    // Linear penalty (sigwh ratio) would give 2×; exponential must exceed 4×.
    EXPECT_GT(w12, 4.0 * w6) << "w12=" << w12 << " w6=" << w6;
}

// ---------------------------------------------------------------------------
// RoutingPreference — blended weights produce correct route preference.
//
// 4-node graph: O, M (on great circle), D, B (bypass, off great circle).
// No direct O→D edge forces a route through either M or B.
// With tight σ_perp=15nm, a storm at M's timestep (ts=12) should raise
// O→M and M→D weights above the bypass path O→B→D.
// ---------------------------------------------------------------------------

// Build a 4-node graph with two symmetric paths of equal total distance:
//
//   Path 1 (via M_near): O ──160nm──▶ M_near ──320nm──▶ D   total=480nm
//   Path 2 (via M_far):  O ──320nm──▶ M_far  ──160nm──▶ D   total=480nm
//
//   Node indices:  0=O  1=M_near  2=D  3=M_far
//   Lat/lon:       O=(51.5,0)  M_near=(51.5,4.28)  D=(51.5,12.84)  M_far=(51.5,8.56)
//
// All nodes are on the same parallel (51.5°N), so:
//   - Both paths have exactly the same total distance in calm weather.
//   - A storm at ts=12 (~t_mean of M_near, 168nm from O) penalises the long
//     M_near→D leg (320nm, traversed when the vessel is at M_near at ts≈11h)
//     but barely affects M_far→D (320nm from O → t_mean≈23h, far from ts=12).
//
// This tests the purely along-track discrimination: σ_perp cancels in the
// blended-weight ratio (constant for all timesteps), so route preference
// inversion must come from different along-track distances, not lateral offset.
static maritime::graph_builder::GraphData make_four_node_graph()
{
    maritime::graph_builder::GraphData g;
    g.lat = {51.5f, 51.5f, 51.5f, 51.5f};
    g.lon = {0.f,   4.28f, 12.84f, 8.56f};

    _Float16 d100 = static_cast<_Float16>(100.f);
    uint16_t d100_bits; std::memcpy(&d100_bits, &d100, 2);
    g.depth = {d100_bits, d100_bits, d100_bits, d100_bits};
    g.flags = {0x00, 0x00, 0x00, 0x00};

    // row_ptr: O→{M_near,M_far}, M_near→D, D→{}, M_far→D
    g.row_ptr = {0, 2, 3, 3, 4};
    // col_idx: O→M_near=1, O→M_far=3, M_near→D=2, M_far→D=2
    g.col_idx = {1, 3, 2, 2};

    g.base_dist_nm = {
        maritime::haversine_nm(51.5f, 0.f,    51.5f, 4.28f),   // O→M_near ~160nm
        maritime::haversine_nm(51.5f, 0.f,    51.5f, 8.56f),   // O→M_far  ~320nm
        maritime::haversine_nm(51.5f, 4.28f,  51.5f, 12.84f),  // M_near→D ~320nm
        maritime::haversine_nm(51.5f, 8.56f,  51.5f, 12.84f),  // M_far→D  ~160nm
    };
    return g;
}

class RoutingPreferenceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmp_  = fs::temp_directory_path() / "maritime_routing_pref_test";
        fs::create_directories(tmp_);
        graph_path_     = (tmp_ / "graph.bin").string();
        flags_path_     = (tmp_ / "flags.bin").string();
        snap_wave_path_ = (tmp_ / "snap_wave.bin").string();
        snap_wind_path_ = (tmp_ / "snap_wind.bin").string();

        auto data = make_four_node_graph();
        maritime::graph_builder::serialise_graph(data, graph_path_, flags_path_);
        write_identity_snaps(snap_wave_path_, snap_wind_path_);
    }
    void TearDown() override { fs::remove_all(tmp_); }

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
    std::string graph_path_, flags_path_, snap_wave_path_, snap_wind_path_;
};

// Both paths O→M_near→D and O→M_far→D have equal total distance (480nm) in calm.
// A storm at ts=12 (≈ vessel-transit time for M_near at ~160nm from O) heavily
// penalises the long M_near→D leg (node M_near, t_mean≈11h, full storm exposure)
// but barely affects M_far→D (node M_far at 320nm, t_mean≈23h, d_along=152nm >>
// σ_along=50nm at ts=12). The storm therefore flips preference to via M_far.
//
// σ_perp cancels in the blended weight ratio (same for all ts at a given node),
// so route inversion here comes purely from along-track distance discrimination.
TEST_F(RoutingPreferenceTest, StormAtMidpointInvertsRoutePreference)
{
    maritime::StaticGraph graph(graph_path_, flags_path_, snap_wave_path_, snap_wind_path_);
    auto vessel = make_vessel();

    // Origin=O(0), destination=D(2)
    const float o_lat = graph.lat()[0], o_lon = graph.lon()[0];
    const float d_lat = graph.lat()[2], d_lon = graph.lon()[2];

    // σ_along=50nm (3.6h at 14kts) concentrates the Gaussian around each node's
    // expected transit time, sharpening the distinction between M_near and M_far.
    constexpr float sa = 50.f, sp = 300.f;

    // --- 1. Calm: both paths equal total distance → equal cost (within 1%) ---
    auto wx_calm = make_wx_uniform(0.f, 0.f, 0.f, 0.f);
    auto w_calm  = maritime::weather_etl::WeightsWriter::compute_blended(
        graph, *wx_calm, vessel, o_lat, o_lon, d_lat, d_lon, sa, sp);

    const double calm_near = static_cast<double>(w_calm[0]) + w_calm[2];
    const double calm_far  = static_cast<double>(w_calm[1]) + w_calm[3];
    EXPECT_NEAR(calm_near, calm_far, calm_near * 0.01)
        << "Calm: both paths same total distance, costs should be equal";

    // --- 2. Storm at ts=12: via M_near becomes expensive ---
    // Edge indices: 0=O→M_near, 1=O→M_far, 2=M_near→D, 3=M_far→D
    auto wx_storm = make_wx_timestep_storm(15.f, 45.f, 270.f, 270.f, 12);
    auto w_storm  = maritime::weather_etl::WeightsWriter::compute_blended(
        graph, *wx_storm, vessel, o_lat, o_lon, d_lat, d_lon, sa, sp);

    EXPECT_GT(static_cast<double>(w_storm[0]) + w_storm[2],
              static_cast<double>(w_storm[1]) + w_storm[3])
        << "Storm at ts=12: via-M_near should be more expensive than via-M_far";

    // --- 3. Key discriminator: M_near→D (long leg, peak at storm time) more
    //    expensive than M_far→D (short leg, peak well after storm). ---
    EXPECT_GT(w_storm[2], w_storm[3])
        << "M_near→D should cost more than M_far→D (long leg sees storm, short leg does not)";

    // --- 4. Dijkstra check: storm → via M_far (0→3→2) ---
    auto dijkstra = [&](const std::vector<uint32_t>& weights) -> std::vector<uint32_t> {
        const uint32_t N = graph.n_nodes();
        std::vector<uint32_t> dist(N, UINT32_MAX);
        std::vector<uint32_t> prev(N, UINT32_MAX);
        dist[0] = 0;
        using P = std::pair<uint32_t, uint32_t>;
        std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
        pq.push({0u, 0u});
        while (!pq.empty()) {
            auto [d, u] = pq.top(); pq.pop();
            if (d > dist[u]) continue;
            for (uint32_t e = graph.row_ptr()[u]; e < graph.row_ptr()[u + 1]; ++e) {
                const uint32_t v  = graph.col_idx()[e];
                const uint32_t nd = d + weights[e];
                if (nd < dist[v]) { dist[v] = nd; prev[v] = u; pq.push({nd, v}); }
            }
        }
        std::vector<uint32_t> path;
        for (uint32_t n = 2u; n != UINT32_MAX; n = prev[n]) path.push_back(n);
        std::reverse(path.begin(), path.end());
        return path;
    };

    const auto path_storm = dijkstra(w_storm);
    ASSERT_EQ(path_storm.size(), 3u);
    EXPECT_EQ(path_storm[0], 0u);
    EXPECT_EQ(path_storm[1], 3u);  // via M_far
    EXPECT_EQ(path_storm[2], 2u);
}

} // namespace
