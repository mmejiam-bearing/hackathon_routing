#include "voyage_planner.hpp"

#include "maritime/routing_engine.hpp"
#include "maritime/static_graph.hpp"
#include "maritime/weather_manager.hpp"

#include "avg_weather_loader.hpp"
#include "weights_writer.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

namespace maritime::router_server {

namespace {

// O(n) nearest-node scan, same approach as route_query.cpp.
uint32_t nearest_node(const maritime::StaticGraph& g, float lat, float lon)
{
    uint32_t best   = 0;
    float    best2  = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < g.n_nodes(); ++i) {
        const float dlat = g.lat()[i] - lat;
        float       dlon = g.lon()[i] - lon;
        if (dlon >  180.f) dlon -= 360.f;
        if (dlon < -180.f) dlon += 360.f;
        const float d2 = dlat*dlat + dlon*dlon;
        if (d2 < best2) { best2 = d2; best = i; }
    }
    return best;
}

} // namespace

VoyagePlan plan_voyage(const VoyageRequest& req)
{
    if (req.avg_weather_dir.empty())
        throw std::invalid_argument("plan_voyage: avg_weather_dir is required");

    maritime::RoutingEngine engine(
        req.graph_path, req.flags_path,
        req.snap_wave_path, req.snap_wind_path, req.cch_path);
    const maritime::StaticGraph& graph = engine.graph();

    const uint32_t src = nearest_node(graph, req.from_lat, req.from_lon);
    const uint32_t dst = nearest_node(graph, req.to_lat,   req.to_lon);

    const std::chrono::sys_days start_date{
        std::chrono::year{req.start_year} /
        std::chrono::month{static_cast<unsigned>(req.start_month)} /
        std::chrono::day{static_cast<unsigned>(req.start_day)}};

    uint32_t current_node = src;
    int      period_idx   = 0;
    std::shared_ptr<maritime::WeatherBuffer> current_wx;

    VoyagePlan plan;

    while (current_node != dst && period_idx < req.max_periods) {
        const std::chrono::year_month_day current_date{
            start_date + std::chrono::days{period_idx}};

        current_wx = maritime::weather_etl::AvgWeatherLoader::load(
            req.avg_weather_dir,
            static_cast<int>(current_date.year()),
            static_cast<unsigned>(current_date.month()),
            static_cast<unsigned>(current_date.day()));
        auto weights = maritime::weather_etl::WeightsWriter::compute_blended(
            graph, *current_wx, req.vessel,
            graph.lat()[current_node], graph.lon()[current_node],
            graph.lat()[dst],          graph.lon()[dst],
            req.sigma_along_nm, req.sigma_perp_nm);
        engine.update_weights(std::move(weights));
        engine.update_weather(current_wx);

        const maritime::CchRouteRequest creq{
            .origin_node    = current_node,
            .dest_node      = dst,
            .base_time_step = 0,
            .vessel         = req.vessel,
        };
        const auto result = engine.route(creq);

        if (result.status != maritime::CchRouteResult::Status::Ok)
            break;   // leave reached_destination = false; caller decides how to report

        DaySegment seg;
        seg.day = period_idx;

        float elapsed_h = 0.f;

        for (std::size_t i = 0; i + 1 < result.node_path.size(); ++i) {
            const uint32_t from = result.node_path[i];
            const uint32_t to   = result.node_path[i + 1];

            const float d_nm = maritime::haversine_nm(
                graph.lat()[from], graph.lon()[from],
                graph.lat()[to],   graph.lon()[to]);

            seg.lat.push_back(graph.lat()[from]);
            seg.lon.push_back(graph.lon()[from]);

            maritime::EdgeCost ec{0.f, d_nm / req.vessel.service_speed_kts};
            if (current_wx) {
                const int ts = std::clamp(static_cast<int>(i),
                                          0, maritime::WX_N_TIMESTEPS - 1);
                ec = maritime::compute_edge_cost(
                    from, to, ts, graph, *current_wx, req.vessel);
            }
            seg.foc_mt  += ec.foc_mt;
            seg.time_h  += ec.time_h;
            seg.dist_nm += d_nm;
            elapsed_h   += ec.time_h;
            current_node = to;

            if (to == dst) {
                seg.lat.push_back(graph.lat()[to]);
                seg.lon.push_back(graph.lon()[to]);
                break;
            }

            if (elapsed_h >= 24.f)
                break;
        }

        plan.total_dist_nm += seg.dist_nm;
        plan.total_foc_mt  += seg.foc_mt;
        plan.total_time_h  += seg.time_h;
        plan.segments.push_back(std::move(seg));
        ++period_idx;
    }

    plan.reached_destination = (current_node == dst);
    return plan;
}

} // namespace maritime::router_server
