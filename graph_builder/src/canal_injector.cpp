#include "canal_injector.hpp"

#include "maritime/edge_weight.hpp"    // maritime::haversine_nm
#include "maritime/static_graph.hpp"   // FLAG_CANAL_TRANSIT

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace maritime::graph_builder {

namespace {

using Pt = std::array<float, 2>;  // {lat, lon}

// ---------------------------------------------------------------------------
// Passage waypoint chains — {lat, lon} order, WGS84 degrees.
//
// Source: marnet_searoute.geojson (searoute-py, Apache 2.0), extracted
//         2026-06-11. Kiel Canal coordinates added manually.
// ---------------------------------------------------------------------------

static constexpr std::array<Pt, 6> SUEZ_PTS{{
    {27.00f,  34.50f}, {27.90f,  33.75f}, {29.70f,  32.60f},
    {30.21f,  32.56f}, {30.32f,  32.38f}, {30.95f,  32.31f},
}};

static constexpr std::array<Pt, 11> PANAMA_PTS{{
    { 9.75f, -80.00f}, { 9.21f, -79.90f}, { 9.17f, -79.82f},
    { 9.12f, -79.80f}, { 9.12f, -79.74f}, { 9.11f, -79.69f},
    { 9.04f, -79.64f}, { 8.99f, -79.59f}, { 8.80f, -79.49f},
    { 8.60f, -79.50f}, { 7.00f, -80.00f},
}};

static constexpr std::array<Pt, 7> BOSPHORUS_PTS{{
    {41.24f,  29.13f}, {41.19f,  29.10f}, {41.15f,  29.05f},
    {41.12f,  29.07f}, {41.05f,  29.04f}, {41.03f,  29.00f},
    {40.97f,  28.98f},
}};

static constexpr std::array<Pt, 4> DARDANELLES_PTS{{
    {40.10f,  26.20f}, {40.81f,  28.45f}, {40.82f,  28.50f}, {40.97f,  28.98f},
}};

static constexpr std::array<Pt, 2> MALACCA_PTS{{
    {3.20f, 100.60f}, {7.00f,  97.00f},
}};

static constexpr std::array<Pt, 3> HORMUZ_PTS{{
    {26.40f,  56.40f}, {26.42f,  56.76f}, {25.50f,  57.10f},
}};

static constexpr std::array<Pt, 4> GIBRALTAR_PTS{{
    {35.95f,  -5.75f}, {35.97f,  -5.35f}, {35.97f,  -5.27f}, {36.00f,  -4.70f},
}};

static constexpr std::array<Pt, 2> BABELMANDAB_PTS{{
    {12.00f,  45.00f}, {12.63f,  43.47f},
}};

static constexpr std::array<Pt, 3> KIEL_PTS{{
    {54.37f,  10.15f}, {54.30f,   9.66f}, {53.89f,   9.13f},
}};

// Non-owning descriptors — std::string_view and std::span are C++20/23 standard
// types. No user-defined destructor, copy, or move (Rule of Zero applies).
struct WaypointChain {
    std::string_view    name;
    std::span<const Pt> pts;
    float               depth_m;
};

static const std::array<WaypointChain, 9> CHAINS{{
    {"SUEZ",        std::span<const Pt>{SUEZ_PTS},        24.0f},
    {"PANAMA",      std::span<const Pt>{PANAMA_PTS},      13.7f},
    {"BOSPHORUS",   std::span<const Pt>{BOSPHORUS_PTS},   27.5f},
    {"DARDANELLES", std::span<const Pt>{DARDANELLES_PTS}, 18.0f},
    {"MALACCA",     std::span<const Pt>{MALACCA_PTS},     23.0f},
    {"HORMUZ",      std::span<const Pt>{HORMUZ_PTS},      30.0f},
    {"GIBRALTAR",   std::span<const Pt>{GIBRALTAR_PTS},   30.0f},
    {"BABELMANDAB", std::span<const Pt>{BABELMANDAB_PTS}, 30.0f},
    {"KIEL",        std::span<const Pt>{KIEL_PTS},        11.0f},
}};

// Returns indices of the k ocean nodes nearest to (lat, lon) by haversine.
// O(n_ocean + k log k) — called ~18 times per graph build, fine for offline use.
[[nodiscard]] std::vector<uint32_t> k_nearest_ocean(
    const GraphData& data,
    uint32_t         n_ocean,
    float            lat,
    float            lon,
    int              k)
{
    std::vector<std::pair<float, uint32_t>> dists;
    dists.reserve(n_ocean);
    for (uint32_t i = 0; i < n_ocean; ++i) {
        dists.emplace_back(
            maritime::haversine_nm(lat, lon, data.lat[i], data.lon[i]), i);
    }

    const auto kk = std::min<std::ptrdiff_t>(k, static_cast<std::ptrdiff_t>(dists.size()));
    std::partial_sort(dists.begin(), dists.begin() + kk, dists.end());

    std::vector<uint32_t> result;
    result.reserve(static_cast<std::size_t>(kk));
    for (auto it = dists.begin(); it != dists.begin() + kk; ++it)
        result.push_back(it->second);
    return result;
}

} // namespace

// ---------------------------------------------------------------------------

std::vector<uint32_t> inject_canal_nodes(GraphData& data)
{
    std::vector<uint32_t> ids;
    ids.reserve(42);

    for (const auto& chain : CHAINS) {
        for (const auto& pt : chain.pts) {
            const uint32_t nid = static_cast<uint32_t>(data.lat.size());
            ids.push_back(nid);

            data.lat.push_back(pt[0]);
            data.lon.push_back(pt[1]);

            uint16_t depth_bits = 0;
            {
                _Float16 d16 = static_cast<_Float16>(chain.depth_m);
                std::memcpy(&depth_bits, &d16, sizeof(uint16_t));
            }
            data.depth.push_back(depth_bits);
            data.flags.push_back(static_cast<uint8_t>(FLAG_CANAL_TRANSIT));
        }
    }

    return ids;
}

void add_canal_edges_to_adj(
    std::vector<std::vector<uint32_t>>& adj,
    const GraphData&                    data,
    const std::vector<uint32_t>&        canal_ids,
    uint32_t                            n_ocean,
    int                                 k)
{
    // Adds a bidirectional edge between u and v in adj.
    auto link = [&](uint32_t u, uint32_t v) {
        adj[u].push_back(v);
        adj[v].push_back(u);
    };

    std::size_t id_offset = 0;
    for (const auto& chain : CHAINS) {
        const std::size_t n = chain.pts.size();

        // Intra-chain edges — connect consecutive waypoints bidirectionally.
        for (std::size_t i = 0; i + 1 < n; ++i)
            link(canal_ids[id_offset + i], canal_ids[id_offset + i + 1]);

        // Ocean endpoint connections — connect the first and last waypoints
        // of each chain to the K nearest ocean nodes on their respective sides.
        const uint32_t first = canal_ids[id_offset];
        const uint32_t last  = canal_ids[id_offset + n - 1];

        for (uint32_t o : k_nearest_ocean(data, n_ocean, data.lat[first], data.lon[first], k))
            link(first, o);
        for (uint32_t o : k_nearest_ocean(data, n_ocean, data.lat[last], data.lon[last], k))
            link(last, o);

        id_offset += n;
    }
}

} // namespace maritime::graph_builder
