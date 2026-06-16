#pragma once

#include "maritime/mmap_region.hpp"
#include "maritime/weather_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <stdexcept>

namespace maritime {

// ---------------------------------------------------------------------------
// Node flag bitmask — stored in flags.bin, one byte per graph node.
// ---------------------------------------------------------------------------
enum NodeFlags : uint8_t {
    FLAG_ECA            = 0x01,  // Emission Control Area
    FLAG_TSS            = 0x02,  // Traffic Separation Scheme
    FLAG_RESTRICTED     = 0x04,  // RESARE / ATBA
    FLAG_CANAL_TRANSIT  = 0x08,  // Enclosed waterway — suppress weather lookup
    FLAG_LOW_CONF_WX    = 0x10,  // Snapped >2 cells — warn in response
};

// ---------------------------------------------------------------------------
// On-disk layout of graph.bin
//
// [Header: GraphHeader]
// [float32 lat[n_nodes]]
// [float32 lon[n_nodes]]
// [float16 depth[n_nodes]]
// [uint32  row_ptr[n_nodes + 1]]
// [uint32  col_idx[n_edges]]
// [float32 base_dist_nm[n_edges]]   great-circle distance, weather-independent
// ---------------------------------------------------------------------------
struct GraphHeader {
    uint32_t magic;       // 0x4752414D "MARG"
    uint32_t version;     // schema version, currently 1
    uint32_t n_nodes;
    uint32_t n_edges;
};

// ---------------------------------------------------------------------------
// On-disk layout of snap_wave.bin / snap_wind.bin
//
// One file per weather grid (wave: 621x1440, wind: 721x1440 — see
// weather_manager.hpp). Two flat arrays, each of length n_lat * n_lon:
// [uint16 snap_lat[n_lat*n_lon]]
// [uint16 snap_lon[n_lat*n_lon]]
//
// For each 0.25° weather grid cell, snap_lat/snap_lon give the nearest
// ocean cell index (row/col, south-first, matching that grid's convention).
// For cells that are already ocean the values are identity.  Built offline
// by graph_builder's snap_table_builder (BFS nearest-ocean-cell flood fill).
// ---------------------------------------------------------------------------
struct SnapHeader {
    uint32_t magic;    // 0x50414E53 "SNAP"
    uint32_t version;
    uint32_t n_lat;    // 621 (wave) or 721 (wind)
    uint32_t n_lon;    // 1440
};

// ---------------------------------------------------------------------------
// StaticGraph
//
// Rule of Zero: all members are either MmapRegion (Rule of Five, move-only)
// or std::span (non-owning view, trivially copyable).  The compiler-
// generated destructor, move constructor, and move assignment operator are
// all correct.  Copy is deleted transitively via MmapRegion.
//
// All spans are non-owning views into the mapped pages.  Their lifetime is
// strictly bounded by the MmapRegion members of this struct — they are
// never handed out independently.
// ---------------------------------------------------------------------------
class StaticGraph {
public:
    explicit StaticGraph(
        const std::string& graph_path,
        const std::string& flags_path,
        const std::string& snap_wave_path,
        const std::string& snap_wind_path)
        : graph_mm_     (graph_path)
        , flags_mm_     (flags_path)
        , snap_wave_mm_ (snap_wave_path)
        , snap_wind_mm_ (snap_wind_path)
    {
        // Validate magic bytes and unpack typed spans
        const auto& hdr = *reinterpret_cast<const GraphHeader*>(graph_mm_.data());
        if (hdr.magic != 0x4752'414Du)
            throw std::runtime_error("graph.bin: bad magic");

        const std::size_t base = sizeof(GraphHeader);
        lat_       = graph_mm_.as_span<float>   (base,                          hdr.n_nodes);
        lon_       = graph_mm_.as_span<float>   (base + hdr.n_nodes*4,          hdr.n_nodes);
        depth_     = graph_mm_.as_span<uint16_t>(base + hdr.n_nodes*8,          hdr.n_nodes);
        row_ptr_   = graph_mm_.as_span<uint32_t>(base + hdr.n_nodes*10,         hdr.n_nodes + 1);
        col_idx_   = graph_mm_.as_span<uint32_t>(base + hdr.n_nodes*10 + (hdr.n_nodes+1)*4,
                                                  hdr.n_edges);
        base_dist_ = graph_mm_.as_span<float>   (base + hdr.n_nodes*10 + (hdr.n_nodes+1)*4
                                                  + hdr.n_edges*4,
                                                  hdr.n_edges);

        flags_     = flags_mm_.as_span<uint8_t>(0, hdr.n_nodes);

        snap_lat_wave_ = load_snap_table(snap_wave_mm_, snap_lon_wave_);
        snap_lat_wind_ = load_snap_table(snap_wind_mm_, snap_lon_wind_);

        n_nodes_ = hdr.n_nodes;
        n_edges_ = hdr.n_edges;
    }

    // Accessors — return non-owning spans; callers must not outlive this object
    [[nodiscard]] uint32_t                    n_nodes()    const noexcept { return n_nodes_;    }
    [[nodiscard]] uint32_t                    n_edges()    const noexcept { return n_edges_;    }
    [[nodiscard]] std::span<const float>      lat()        const noexcept { return lat_;        }
    [[nodiscard]] std::span<const float>      lon()        const noexcept { return lon_;        }
    [[nodiscard]] std::span<const uint16_t>   depth()      const noexcept { return depth_;      }
    [[nodiscard]] std::span<const uint32_t>   row_ptr()    const noexcept { return row_ptr_;    }
    [[nodiscard]] std::span<const uint32_t>   col_idx()    const noexcept { return col_idx_;    }
    [[nodiscard]] std::span<const float>      base_dist()  const noexcept { return base_dist_;  }
    [[nodiscard]] std::span<const uint8_t>    flags()      const noexcept { return flags_;      }

    // O(1) wave-grid index for a graph node, honouring the wave snap table.
    // lon convention: graph stores -180..180, weather grid is 0..360.
    [[nodiscard]] std::size_t wave_weather_idx(
        uint32_t node_id,
        int      time_step) const noexcept
    {
        return grid_idx(node_id, time_step, WAVE_NJ, WAVE_LAT_MIN,
                         snap_lat_wave_, snap_lon_wave_);
    }

    // O(1) wind-grid index for a graph node, honouring the wind snap table.
    [[nodiscard]] std::size_t wind_weather_idx(
        uint32_t node_id,
        int      time_step) const noexcept
    {
        return grid_idx(node_id, time_step, WIND_NJ, WIND_LAT_MIN,
                         snap_lat_wind_, snap_lon_wind_);
    }

private:
    // Shared south-first lat/lon -> flat-index lookup, parameterised by grid
    // shape/extent so wave (621 rows, -75°) and wind (721 rows, -90°) reuse
    // the same logic.
    [[nodiscard]] std::size_t grid_idx(
        uint32_t node_id, int time_step,
        int nj, float lat_min,
        std::span<const uint16_t> snap_lat,
        std::span<const uint16_t> snap_lon) const noexcept
    {
        constexpr int NI = WX_NI;

        float la = lat_[node_id];
        float lo = lon_[node_id];
        if (lo < 0.f) lo += 360.f;

        int raw_lat_i = static_cast<int>((la - lat_min) / WX_RES);
        int raw_lon_i = static_cast<int>(lo              / WX_RES) % NI;
        raw_lat_i = std::clamp(raw_lat_i, 0, nj - 1);
        raw_lon_i = std::clamp(raw_lon_i, 0, NI - 1);

        // Redirect to nearest valid ocean cell (identity for most nodes)
        const std::size_t raw_idx = static_cast<std::size_t>(raw_lat_i) * NI
                                  + static_cast<std::size_t>(raw_lon_i);
        const int snapped_lat_i = snap_lat[raw_idx];
        const int snapped_lon_i = snap_lon[raw_idx];

        return static_cast<std::size_t>(time_step)
             * static_cast<std::size_t>(nj * NI)
             + static_cast<std::size_t>(snapped_lat_i) * NI
             + static_cast<std::size_t>(snapped_lon_i);
    }

    // Validates a snap table's magic, unpacks snap_lon into out_lon, and
    // returns the snap_lat span. Keeps the two near-identical snap loads in
    // the constructor to one call site each.
    [[nodiscard]] static std::span<const uint16_t> load_snap_table(
        const MmapRegion& mm, std::span<const uint16_t>& out_lon)
    {
        const auto& hdr = *reinterpret_cast<const SnapHeader*>(mm.data());
        if (hdr.magic != 0x5041'4E53u)   // "SNAP" LE
            throw std::runtime_error("snap table: bad magic");

        const std::size_t n_cells = hdr.n_lat * hdr.n_lon;
        out_lon = mm.as_span<uint16_t>(sizeof(SnapHeader) + n_cells * 2, n_cells);
        return mm.as_span<uint16_t>(sizeof(SnapHeader), n_cells);
    }

    // Owning resources — Rule of Five each, move-only
    // Note: no cch_mm_ — CCH topology is built at runtime by RoutingKit
    // from the graph structure, not loaded from a precomputed binary.
    MmapRegion graph_mm_;
    MmapRegion flags_mm_;
    MmapRegion snap_wave_mm_;
    MmapRegion snap_wind_mm_;

    // Non-owning typed views — Rule of Zero, trivially movable
    std::span<const float>    lat_;
    std::span<const float>    lon_;
    std::span<const uint16_t> depth_;
    std::span<const uint32_t> row_ptr_;
    std::span<const uint32_t> col_idx_;
    std::span<const float>    base_dist_;
    std::span<const uint8_t>  flags_;
    std::span<const uint16_t> snap_lat_wave_;
    std::span<const uint16_t> snap_lon_wave_;
    std::span<const uint16_t> snap_lat_wind_;
    std::span<const uint16_t> snap_lon_wind_;

    uint32_t n_nodes_ = 0;
    uint32_t n_edges_ = 0;
};

} // namespace maritime
