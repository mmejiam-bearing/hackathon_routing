#pragma once

#include "graph_serialiser.hpp"

#include <cstdint>
#include <vector>

namespace maritime::graph_builder {

// Appends 42 FLAG_CANAL_TRANSIT nodes (9 passage chains) to data.
// Returns the assigned node IDs in chain order:
//   [suez_0..suez_5, panama_0..panama_10, bosphorus_0..bosphorus_6,
//    dardanelles_0..dardanelles_3, malacca_0..malacca_1, hormuz_0..hormuz_2,
//    gibraltar_0..gibraltar_3, babelmandab_0..babelmandab_1, kiel_0..kiel_2]
[[nodiscard]] std::vector<uint32_t>
inject_canal_nodes(GraphData& data);

// Adds canal chain edges and ocean-endpoint connections into adj.
// adj must be sized to data.n_nodes() (ocean + canal nodes).
// Modifies adj[canal_node] with intra-chain and ocean-connection edges.
// Also appends canal-endpoint entries to adj[ocean_node] for the K
// nearest ocean nodes at each chain terminus.
void add_canal_edges_to_adj(
    std::vector<std::vector<uint32_t>>& adj,
    const GraphData& data,
    const std::vector<uint32_t>& canal_ids,
    uint32_t n_ocean,
    int k = 3);

} // namespace maritime::graph_builder
