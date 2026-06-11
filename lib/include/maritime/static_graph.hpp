#pragma once

#include "maritime/mmap_region.hpp"

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
// On-disk layout of snap.bin
//
// Two flat arrays, each of length (721 * 1440) = 1,038,240 uint16 values.
// [uint16 snap_lat[721*1440]]
// [uint16 snap_lon[721*1440]]
//
// For each 0.25° weather grid cell, snap_lat/snap_lon give the nearest
// ocean cell index.  For cells that are already ocean the values are
// identity.  Built offline by scipy.ndimage.distance_transform_edt.
// ---------------------------------------------------------------------------
struct SnapHeader {
    uint32_t magic;    // 0x50414E53 "SNAP"
    uint32_t version;
    uint32_t n_lat;    // 721
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
        const std::string& snap_path)
        : graph_mm_ (graph_path)
        , flags_mm_ (flags_path)
        , snap_mm_  (snap_path)
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

        const auto& snap_hdr =
            *reinterpret_cast<const SnapHeader*>(snap_mm_.data());
        if (snap_hdr.magic != 0x5041'4E53u)   // "SNAP" LE
            throw std::runtime_error("snap.bin: bad magic");

        const std::size_t n_cells = snap_hdr.n_lat * snap_hdr.n_lon;
        snap_lat_  = snap_mm_.as_span<uint16_t>(sizeof(SnapHeader),               n_cells);
        snap_lon_  = snap_mm_.as_span<uint16_t>(sizeof(SnapHeader) + n_cells * 2, n_cells);

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
    [[nodiscard]] std::span<const uint16_t>   snap_lat()   const noexcept { return snap_lat_;   }
    [[nodiscard]] std::span<const uint16_t>   snap_lon()   const noexcept { return snap_lon_;   }

    // O(1) weather-grid index for a graph node, honouring the snap table.
    // lon convention: graph stores -180..180, weather grid is 0..360.
    [[nodiscard]] std::size_t weather_idx(
        uint32_t node_id,
        int      time_step) const noexcept
    {
        constexpr int NI = 1440;
        constexpr int NJ = 721;
        constexpr float WX_RES = 0.25f;

        float la = lat_[node_id];
        float lo = lon_[node_id];
        if (lo < 0.f) lo += 360.f;

        int raw_lat_i = static_cast<int>((90.f - la) / WX_RES);
        int raw_lon_i = static_cast<int>(lo          / WX_RES) % NI;
        raw_lat_i = std::clamp(raw_lat_i, 0, NJ - 1);
        raw_lon_i = std::clamp(raw_lon_i, 0, NI - 1);

        // Redirect to nearest valid ocean cell (identity for most nodes)
        const std::size_t raw_idx = static_cast<std::size_t>(raw_lat_i) * NI
                                  + static_cast<std::size_t>(raw_lon_i);
        const int snapped_lat_i = snap_lat_[raw_idx];
        const int snapped_lon_i = snap_lon_[raw_idx];

        return static_cast<std::size_t>(time_step)
             * static_cast<std::size_t>(NJ * NI)
             + static_cast<std::size_t>(snapped_lat_i) * NI
             + static_cast<std::size_t>(snapped_lon_i);
    }

private:
    // Owning resources — Rule of Five each, move-only
    // Note: no cch_mm_ — CCH topology is built at runtime by RoutingKit
    // from the graph structure, not loaded from a precomputed binary.
    MmapRegion graph_mm_;
    MmapRegion flags_mm_;
    MmapRegion snap_mm_;

    // Non-owning typed views — Rule of Zero, trivially movable
    std::span<const float>    lat_;
    std::span<const float>    lon_;
    std::span<const uint16_t> depth_;
    std::span<const uint32_t> row_ptr_;
    std::span<const uint32_t> col_idx_;
    std::span<const float>    base_dist_;
    std::span<const uint8_t>  flags_;
    std::span<const uint16_t> snap_lat_;
    std::span<const uint16_t> snap_lon_;

    uint32_t n_nodes_ = 0;
    uint32_t n_edges_ = 0;
};

} // namespace maritime
