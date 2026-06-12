#pragma once

#include "maritime/static_graph.hpp"
#include "maritime/weather_manager.hpp"
#include "maritime/edge_weight.hpp"

#include <routingkit/customizable_contraction_hierarchy.h>
#include <routingkit/inverse_vector.h>
#include <routingkit/geo_position_to_node.h>
#include <routingkit/vector_io.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(__cpp_lib_atomic_shared_ptr)
#include <shared_mutex>
#endif

namespace maritime {

// ---------------------------------------------------------------------------
// CchIndex — Rule of Zero
//
// Holds the RoutingKit CCH topology (built once from graph structure).
//
// RoutingKit intentionally provides no CCH serialisation: "store the order
// and rebuild the CCH as needed."  graph_builder saves the node order vector
// via save_vector(); router_server loads it and reconstructs the CCH.
// ---------------------------------------------------------------------------
class CchIndex {
public:
    // Build from graph structure + pre-computed node order (graph_builder path).
    explicit CchIndex(
        const std::vector<uint32_t>& order,
        const std::vector<uint32_t>& tail,
        const std::vector<uint32_t>& head)
        : topology_(
            std::vector<unsigned>(order.begin(), order.end()),
            std::vector<unsigned>(tail.begin(), tail.end()),
            std::vector<unsigned>(head.begin(), head.end()))
    {}

    // Load node order from file, reconstruct topology (router_server path).
    // tail and head must be the same graph used when the order was computed.
    CchIndex(
        const std::string& order_path,
        const std::vector<uint32_t>& tail,
        const std::vector<uint32_t>& head)
        : topology_(
            RoutingKit::load_vector<unsigned>(order_path),
            std::vector<unsigned>(tail.begin(), tail.end()),
            std::vector<unsigned>(head.begin(), head.end()))
    {}

    [[nodiscard]]
    RoutingKit::CustomizableContractionHierarchyMetric
    customise(const std::vector<uint32_t>& weights) const
    {
        // Named vector required: CCHMetric stores a raw pointer to the data.
        // keep it alive through the customize() call.
        std::vector<unsigned> w(weights.begin(), weights.end());
        RoutingKit::CustomizableContractionHierarchyMetric metric{topology_, w};
        metric.customize();   // populate forward/backward arrays from weights
        return metric;
    }

    [[nodiscard]]
    const RoutingKit::CustomizableContractionHierarchy&
    topology() const noexcept { return topology_; }

private:
    RoutingKit::CustomizableContractionHierarchy topology_;
};

// ---------------------------------------------------------------------------
// CchQueryState — Rule of Zero, per-thread, never shared
// ---------------------------------------------------------------------------
class CchQueryState {
public:
    explicit CchQueryState(
        const RoutingKit::CustomizableContractionHierarchyMetric& metric)
        : query_(metric)
    {}

    [[nodiscard]] std::vector<uint32_t> run(uint32_t source, uint32_t target)
    {
        query_.reset().add_source(source).add_target(target).run();
        if (query_.get_distance() == RoutingKit::inf_weight)
            return {};
        return query_.get_node_path();
    }

    // Non-const: RoutingKit::CCHQuery::get_distance() is not marked const.
    [[nodiscard]] uint32_t last_distance() noexcept
    {
        return query_.get_distance();
    }

private:
    RoutingKit::CustomizableContractionHierarchyQuery query_;
};

// ---------------------------------------------------------------------------
// CchRouteRequest / CchRouteResult — Rule of Zero, plain aggregates
// ---------------------------------------------------------------------------
struct CchRouteRequest {
    uint32_t     origin_node;
    uint32_t     dest_node;
    int          base_time_step;
    VesselParams vessel;
};

struct CchRouteResult {
    enum class Status { Ok, NoRoute, WeatherUnavailable };

    Status                status        = Status::Ok;
    std::vector<uint32_t> node_path;
    std::vector<float>    waypoint_lat;
    std::vector<float>    waypoint_lon;
    float                 total_foc_mt  = 0.f;
    float                 total_dist_nm = 0.f;
    float                 total_time_h  = 0.f;
    std::vector<uint8_t>  segment_flags;
};

// ---------------------------------------------------------------------------
// RoutingEngine — Rule of Zero
//
// Top-level facade used by the router_server process.
// Loads graph artifacts from disk, loads serialised CCH node order and
// reconstructs the topology, then manages the atomic CCH metric swap on
// weather updates.
//
// update_weights() is called by the server's polling thread when a new
// weights.bin arrives from the weather ETL.  It rebuilds the CCH metric
// (~seconds) on the calling thread, then atomically swaps the pointer.
// In-flight queries hold their own shared_ptr and complete uninterrupted.
// ---------------------------------------------------------------------------
class RoutingEngine {
public:
    // Constructor used by router_server:
    //   cch_topo_path — file written by graph_builder's CchPreprocessor::save()
    //                   contains the RoutingKit node order vector.
    explicit RoutingEngine(
        const std::string& graph_path,
        const std::string& flags_path,
        const std::string& snap_path,
        const std::string& cch_topo_path)
        : graph_   (graph_path, flags_path, snap_path)
        , cch_     (cch_topo_path,
                    extract_tail(graph_),
                    std::vector<uint32_t>(
                        graph_.col_idx().begin(), graph_.col_idx().end()))
        , metric_  (nullptr)
        , generation_(0)
    {}

    // Update the live WeatherBuffer used by route() for FOC accumulation.
    // Thread-safe: atomic store; in-flight queries retain their shared_ptr.
    void update_weather(std::shared_ptr<WeatherBuffer> buf) noexcept
    {
        weather_manager_.update(std::move(buf));
    }

    // Update CCH metric from a new edge weight vector (weather ETL output).
    // Thread-safe: can be called from the polling thread while query threads run.
    void update_weights(std::vector<uint32_t> weights, int64_t base_epoch = 0)
    {
        (void)base_epoch;
        auto new_metric = std::make_shared<
            RoutingKit::CustomizableContractionHierarchyMetric>(
                cch_.customise(weights));

#if defined(__cpp_lib_atomic_shared_ptr)
        metric_.store(std::move(new_metric), std::memory_order_release);
#else
        {
            std::unique_lock lock(metric_mutex_);
            metric_ = std::move(new_metric);
        }
#endif
        generation_.fetch_add(1, std::memory_order_release);
    }

    // Serve a route query.  Thread-safe — uses thread_local query state.
    [[nodiscard]] CchRouteResult route(const CchRouteRequest& req)
    {
#if defined(__cpp_lib_atomic_shared_ptr)
        auto metric = metric_.load(std::memory_order_acquire);
#else
        std::shared_ptr<RoutingKit::CustomizableContractionHierarchyMetric> metric;
        {
            std::shared_lock lock(metric_mutex_);
            metric = metric_;
        }
#endif
        if (!metric)
            return CchRouteResult{
                .status = CchRouteResult::Status::WeatherUnavailable};

        CchQueryState& qstate = get_thread_query_state(*metric);

        auto node_path = qstate.run(req.origin_node, req.dest_node);
        if (node_path.empty())
            return CchRouteResult{.status = CchRouteResult::Status::NoRoute};

        CchRouteResult result;
        result.status    = CchRouteResult::Status::Ok;
        result.node_path = std::move(node_path);
        result.waypoint_lat.reserve(result.node_path.size());
        result.waypoint_lon.reserve(result.node_path.size());
        result.segment_flags.reserve(result.node_path.size());

        for (std::size_t i = 0; i < result.node_path.size(); ++i) {
            const uint32_t n = result.node_path[i];
            result.waypoint_lat.push_back(graph_.lat()[n]);
            result.waypoint_lon.push_back(graph_.lon()[n]);
            result.segment_flags.push_back(graph_.flags()[n]);
        }

        // FOC and distance accumulation.
        // If no WeatherBuffer has been loaded yet, foc and dist stay 0
        // (geometry is still returned — soft degradation, not an error).
        if (auto wx = weather_manager_.acquire()) {
            for (std::size_t i = 0; i + 1 < result.node_path.size(); ++i) {
                const uint32_t from = result.node_path[i];
                const uint32_t to   = result.node_path[i + 1];
                const int ts = std::clamp(
                    req.base_time_step + static_cast<int>(i),
                    0, WX_N_TIMESTEPS - 1);
                const EdgeCost ec =
                    compute_edge_cost(from, to, ts, graph_, *wx, req.vessel);
                result.total_foc_mt  += ec.foc_mt;
                result.total_time_h  += ec.time_h;
                result.total_dist_nm +=
                    haversine_nm(graph_.lat()[from], graph_.lon()[from],
                                 graph_.lat()[to],   graph_.lon()[to]);
            }
        }
        return result;
    }

    [[nodiscard]] const StaticGraph& graph() const noexcept { return graph_; }

private:
    // Derive the CSR tail array from row_ptr — O(n_edges), called once at startup.
    static std::vector<uint32_t> extract_tail(const StaticGraph& g)
    {
        std::vector<uint32_t> tail;
        tail.reserve(g.n_edges());
        for (uint32_t u = 0; u < g.n_nodes(); ++u)
            for (uint32_t e = g.row_ptr()[u]; e < g.row_ptr()[u + 1]; ++e)
                tail.push_back(u);
        return tail;
    }

    CchQueryState& get_thread_query_state(
        const RoutingKit::CustomizableContractionHierarchyMetric& metric)
    {
        thread_local uint64_t                        tl_gen{UINT64_MAX};
        thread_local std::unique_ptr<CchQueryState>  tl_state{nullptr};

        const uint64_t gen = generation_.load(std::memory_order_acquire);
        if (tl_gen != gen || !tl_state) {
            tl_state = std::make_unique<CchQueryState>(metric);
            tl_gen   = gen;
        }
        return *tl_state;
    }

    StaticGraph    graph_;
    CchIndex       cch_;
    WeatherManager weather_manager_;

    // C++20 std::atomic<shared_ptr<T>> specialisation is lock-free on x86-64
    // and AArch64.  Fall back to shared_mutex on platforms that don't have it.
#if defined(__cpp_lib_atomic_shared_ptr)
    std::atomic<std::shared_ptr<
        RoutingKit::CustomizableContractionHierarchyMetric>> metric_;
#else
    mutable std::shared_mutex metric_mutex_;
    std::shared_ptr<RoutingKit::CustomizableContractionHierarchyMetric> metric_;
#endif
    std::atomic<uint64_t> generation_;
};

} // namespace maritime
