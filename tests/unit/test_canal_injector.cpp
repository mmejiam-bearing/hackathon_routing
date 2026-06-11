#include "graph_builder/src/canal_injector.hpp"
#include "graph_builder/src/graph_serialiser.hpp"
#include "maritime/static_graph.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

// Builds a small synthetic ocean graph with nodes spread near the major
// canal regions.  CSR edges are intentionally absent — inject_canal_nodes
// only needs the node coordinate arrays.
maritime::graph_builder::GraphData make_ocean_graph()
{
    maritime::graph_builder::GraphData g;

    // 10 ocean nodes: one near each major passage endpoint
    g.lat = {28.0f,  9.5f, 41.5f, 40.0f,  3.0f,
             26.0f, 36.0f, 12.5f, 54.0f,  7.5f};
    g.lon = {33.0f, -80.5f, 29.5f, 26.0f, 101.0f,
             57.0f, -6.0f,  44.5f, 10.0f, -80.5f};

    _Float16 d20 = static_cast<_Float16>(20.0f);
    uint16_t d20_bits;
    std::memcpy(&d20_bits, &d20, sizeof(uint16_t));
    g.depth.assign(10, d20_bits);
    g.flags.assign(10, 0u);

    // row_ptr for 10 nodes with no edges
    g.row_ptr.assign(11, 0u);

    return g;
}

} // namespace

// ---------------------------------------------------------------------------

TEST(CanalInjector, TotalNodeCount)
{
    auto g = make_ocean_graph();
    const std::size_t ocean_before = g.lat.size();
    const auto ids = maritime::graph_builder::inject_canal_nodes(g);

    EXPECT_EQ(ids.size(), 42u);
    EXPECT_EQ(g.lat.size(),  ocean_before + 42u);
    EXPECT_EQ(g.lon.size(),  ocean_before + 42u);
    EXPECT_EQ(g.depth.size(), ocean_before + 42u);
    EXPECT_EQ(g.flags.size(), ocean_before + 42u);
}

TEST(CanalInjector, AllFlagsSetToCanal)
{
    auto g = make_ocean_graph();
    const auto ids = maritime::graph_builder::inject_canal_nodes(g);

    for (uint32_t id : ids) {
        EXPECT_EQ(g.flags[id], static_cast<uint8_t>(maritime::FLAG_CANAL_TRANSIT))
            << "Canal node " << id << " missing FLAG_CANAL_TRANSIT";
    }
}

TEST(CanalInjector, OceanNodesUnmodified)
{
    auto g = make_ocean_graph();
    const std::vector<float> orig_lat = g.lat;
    const std::vector<float> orig_lon = g.lon;
    const std::vector<uint8_t> orig_flags = g.flags;

    (void)maritime::graph_builder::inject_canal_nodes(g);

    // First 10 entries (ocean nodes) must be unchanged
    for (std::size_t i = 0; i < orig_lat.size(); ++i) {
        EXPECT_EQ(g.lat[i],   orig_lat[i]);
        EXPECT_EQ(g.lon[i],   orig_lon[i]);
        EXPECT_EQ(g.flags[i], orig_flags[i]);
    }
}

TEST(CanalInjector, DepthsReasonable)
{
    auto g = make_ocean_graph();
    const auto ids = maritime::graph_builder::inject_canal_nodes(g);

    for (uint32_t id : ids) {
        _Float16 d16;
        std::memcpy(&d16, &g.depth[id], sizeof(uint16_t));
        const float depth = static_cast<float>(d16);
        EXPECT_GE(depth, 10.0f)
            << "Canal node " << id << " depth " << depth << " m is too shallow";
        EXPECT_LE(depth, 65504.0f);
    }
}

TEST(CanalInjector, ChainEdgesBidirectional)
{
    auto g = make_ocean_graph();
    const uint32_t n_ocean = static_cast<uint32_t>(g.lat.size());
    const auto ids = maritime::graph_builder::inject_canal_nodes(g);

    std::vector<std::vector<uint32_t>> adj(g.n_nodes());
    maritime::graph_builder::add_canal_edges_to_adj(adj, g, ids, n_ocean);

    // SUEZ chain: ids[0..5].  Check every consecutive pair is bidirectional.
    for (std::size_t i = 0; i + 1 < 6u; ++i) {
        const uint32_t u = ids[i];
        const uint32_t v = ids[i + 1];
        EXPECT_NE(std::find(adj[u].begin(), adj[u].end(), v), adj[u].end())
            << "SUEZ: missing edge " << u << " -> " << v;
        EXPECT_NE(std::find(adj[v].begin(), adj[v].end(), u), adj[v].end())
            << "SUEZ: missing edge " << v << " -> " << u;
    }

    // PANAMA chain starts at ids[6], length 11
    for (std::size_t i = 0; i + 1 < 11u; ++i) {
        const uint32_t u = ids[6 + i];
        const uint32_t v = ids[6 + i + 1];
        EXPECT_NE(std::find(adj[u].begin(), adj[u].end(), v), adj[u].end())
            << "PANAMA: missing edge " << u << " -> " << v;
        EXPECT_NE(std::find(adj[v].begin(), adj[v].end(), u), adj[v].end())
            << "PANAMA: missing edge " << v << " -> " << u;
    }
}

TEST(CanalInjector, OceanEndpointConnections)
{
    auto g = make_ocean_graph();
    const uint32_t n_ocean = static_cast<uint32_t>(g.lat.size());
    const auto ids = maritime::graph_builder::inject_canal_nodes(g);

    std::vector<std::vector<uint32_t>> adj(g.n_nodes());
    maritime::graph_builder::add_canal_edges_to_adj(adj, g, ids, n_ocean, /*k=*/2);

    // Every chain's first and last node must have at least 1 ocean connection.
    // Chain sizes: 6, 11, 7, 4, 2, 3, 4, 2, 3  (total 42)
    const std::size_t chain_sizes[] = {6, 11, 7, 4, 2, 3, 4, 2, 3};
    std::size_t offset = 0;
    for (std::size_t sz : chain_sizes) {
        const uint32_t first = ids[offset];
        const uint32_t last  = ids[offset + sz - 1];

        auto count_ocean = [&](uint32_t node) {
            return static_cast<int>(
                std::count_if(adj[node].begin(), adj[node].end(),
                              [&](uint32_t v){ return v < n_ocean; }));
        };

        EXPECT_GE(count_ocean(first), 1) << "Chain first node " << first << " has no ocean connections";
        EXPECT_LE(count_ocean(first), 2);
        EXPECT_GE(count_ocean(last),  1) << "Chain last node  " << last  << " has no ocean connections";
        EXPECT_LE(count_ocean(last),  2);

        offset += sz;
    }
}

TEST(CanalInjector, NoNonCanalNodeFlagsAltered)
{
    auto g = make_ocean_graph();
    const uint32_t n_ocean = static_cast<uint32_t>(g.lat.size());
    const auto ids = maritime::graph_builder::inject_canal_nodes(g);

    std::vector<std::vector<uint32_t>> adj(g.n_nodes());
    maritime::graph_builder::add_canal_edges_to_adj(adj, g, ids, n_ocean);

    // add_canal_edges_to_adj must not touch flags at all
    for (uint32_t i = 0; i < n_ocean; ++i)
        EXPECT_EQ(g.flags[i], 0u) << "Ocean node " << i << " flag was altered";
}
