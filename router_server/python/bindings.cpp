//
// Python bindings for maritime::router_server.
//
// Exposes a single RoutingEngine class: construct it once from the on-disk
// artifacts (graph/flags/snap/cch + a weights directory, optionally an
// average-weather directory), then call .route(lat, lon, lat, lon) repeatedly.
// Wraps QueryServer (router_server/src/server.hpp) so artifact loading and
// weights.bin parsing are not duplicated here — this module only adds the
// lat/lon -> nearest-node lookup and vessel parameter plumbing needed to
// build a CchRouteRequest from Python.
//
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "server.hpp"
#include "voyage_planner.hpp"

#include "maritime/edge_weight.hpp"
#include "maritime/foc_model.hpp"
#include "maritime/routing_engine.hpp"
#include "maritime/static_graph.hpp"

#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace {

// Same O(n) nearest-node scan used by route_query.cpp / voyage_router.cpp.
uint32_t nearest_node(const maritime::StaticGraph& g, float lat, float lon)
{
    uint32_t best   = 0;
    float    best_d2 = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < g.n_nodes(); ++i) {
        const float dlat = g.lat()[i] - lat;
        float       dlon = g.lon()[i] - lon;
        if (dlon >  180.f) dlon -= 360.f;
        if (dlon < -180.f) dlon += 360.f;
        const float d2 = dlat * dlat + dlon * dlon;
        if (d2 < best_d2) { best_d2 = d2; best = i; }
    }
    return best;
}

template <typename T>
py::array_t<T> to_array(const std::vector<T>& v)
{
    return py::array_t<T>(static_cast<py::ssize_t>(v.size()), v.data());
}

// Shared by PyRoutingEngine::route() and plan_voyage_py() — both build a
// VesselParams from the same flat set of Python kwargs.
maritime::VesselParams make_vessel(
    float service_speed_kts,
    float draft_m, float beam_m, float loa_m,
    float block_coeff, float displacement_t, float transverse_area_m2)
{
    maritime::VesselParams vessel;
    vessel.draft_m            = draft_m;
    vessel.beam_m             = beam_m;
    vessel.loa_m              = loa_m;
    vessel.service_speed_kts  = service_speed_kts;
    vessel.block_coeff        = block_coeff;
    vessel.displacement_t     = displacement_t;
    vessel.transverse_area_m2 = transverse_area_m2;
    vessel.foc_model = [spd = service_speed_kts]
        (float /*sig_wh*/, float wind_spd, float /*curr*/)
        -> std::pair<float, float> {
        return maritime::universal_foc_model(wind_spd, spd);
    };
    return vessel;
}

// Parses "YYYY-MM-DD" into (year, month, day). Throws on malformed input.
void parse_iso_date(const std::string& s, int& year, int& month, int& day)
{
    int y, m, d;
    char dash1, dash2;
    std::istringstream iss(s);
    iss >> y >> dash1 >> m >> dash2 >> d;
    if (!iss || dash1 != '-' || dash2 != '-')
        throw std::invalid_argument("start_date must be YYYY-MM-DD, got: " + s);
    year = y; month = m; day = d;
}

maritime::router_server::VoyagePlan plan_voyage_py(
    const std::string& graph_path,     const std::string& flags_path,
    const std::string& snap_wave_path, const std::string& snap_wind_path,
    const std::string& cch_topo_path,
    const std::string& avg_weather_dir, const std::string& start_date,
    float origin_lat, float origin_lon, float dest_lat, float dest_lon,
    float service_speed_kts,
    float draft_m, float beam_m, float loa_m,
    float block_coeff, float displacement_t, float transverse_area_m2,
    float sigma_along_nm, float sigma_perp_nm, int max_periods)
{
    maritime::router_server::VoyageRequest req;
    req.graph_path      = graph_path;
    req.flags_path      = flags_path;
    req.snap_wave_path  = snap_wave_path;
    req.snap_wind_path  = snap_wind_path;
    req.cch_path        = cch_topo_path;
    req.avg_weather_dir = avg_weather_dir;
    parse_iso_date(start_date, req.start_year, req.start_month, req.start_day);
    req.from_lat   = origin_lat;
    req.from_lon   = origin_lon;
    req.to_lat     = dest_lat;
    req.to_lon     = dest_lon;
    req.vessel     = make_vessel(service_speed_kts, draft_m, beam_m, loa_m,
                                  block_coeff, displacement_t, transverse_area_m2);
    req.sigma_along_nm = sigma_along_nm;
    req.sigma_perp_nm  = sigma_perp_nm;
    req.max_periods    = max_periods;
    return maritime::router_server::plan_voyage(req);
}

} // namespace

class PyRoutingEngine {
public:
    PyRoutingEngine(
        const std::string& graph_path,
        const std::string& flags_path,
        const std::string& snap_wave_path,
        const std::string& snap_wind_path,
        const std::string& cch_topo_path,
        const std::string& weights_dir,
        const std::string& avg_weather_dir,
        uint32_t            poll_interval_s)
        : server_(maritime::router_server::ServerConfig{
              .graph_path      = graph_path,
              .flags_path      = flags_path,
              .snap_wave_path  = snap_wave_path,
              .snap_wind_path  = snap_wind_path,
              .cch_topo_path   = cch_topo_path,
              .weights_dir     = weights_dir,
              .avg_weather_dir = avg_weather_dir,
              .poll_interval_s = poll_interval_s,
          })
        , graph_(graph_path, flags_path, snap_wave_path, snap_wind_path)
    {}

    [[nodiscard]] maritime::CchRouteResult route(
        float origin_lat, float origin_lon,
        float dest_lat,   float dest_lon,
        float service_speed_kts,
        float draft_m, float beam_m, float loa_m,
        float block_coeff, float displacement_t, float transverse_area_m2,
        int   base_time_step)
    {
        const maritime::CchRouteRequest req{
            .origin_node    = nearest_node(graph_, origin_lat, origin_lon),
            .dest_node      = nearest_node(graph_, dest_lat,   dest_lon),
            .base_time_step = base_time_step,
            .vessel         = make_vessel(service_speed_kts, draft_m, beam_m, loa_m,
                                           block_coeff, displacement_t, transverse_area_m2),
        };
        return server_.serve_query(req);
    }

    [[nodiscard]] uint32_t n_nodes() const noexcept { return graph_.n_nodes(); }
    [[nodiscard]] uint32_t n_edges() const noexcept { return graph_.n_edges(); }

private:
    maritime::router_server::QueryServer server_;
    maritime::StaticGraph                graph_;
};

PYBIND11_MODULE(maritime_router, m)
{
    m.doc() = "Native bindings for maritime router_server: lat/lon -> CCH route, "
               "with the weather samples that drove the cost at each waypoint.";

    py::enum_<maritime::CchRouteResult::Status>(m, "Status")
        .value("OK",                  maritime::CchRouteResult::Status::Ok)
        .value("NO_ROUTE",            maritime::CchRouteResult::Status::NoRoute)
        .value("WEATHER_UNAVAILABLE", maritime::CchRouteResult::Status::WeatherUnavailable);

    py::class_<maritime::CchRouteResult>(m, "RouteResult")
        .def_readonly("status",        &maritime::CchRouteResult::status)
        .def_property_readonly("waypoint_lat",
            [](const maritime::CchRouteResult& r) { return to_array(r.waypoint_lat); })
        .def_property_readonly("waypoint_lon",
            [](const maritime::CchRouteResult& r) { return to_array(r.waypoint_lon); })
        .def_property_readonly("segment_flags",
            [](const maritime::CchRouteResult& r) { return to_array(r.segment_flags); })
        .def_property_readonly("sig_wh",
            [](const maritime::CchRouteResult& r) { return to_array(r.sig_wh); })
        .def_property_readonly("wind_spd",
            [](const maritime::CchRouteResult& r) { return to_array(r.wind_spd); })
        .def_property_readonly("wind_dir",
            [](const maritime::CchRouteResult& r) { return to_array(r.wind_dir); })
        .def_property_readonly("wave_dir",
            [](const maritime::CchRouteResult& r) { return to_array(r.wave_dir); })
        .def_readonly("total_foc_mt",  &maritime::CchRouteResult::total_foc_mt)
        .def_readonly("total_dist_nm", &maritime::CchRouteResult::total_dist_nm)
        .def_readonly("total_time_h",  &maritime::CchRouteResult::total_time_h);

    py::class_<PyRoutingEngine>(m, "RoutingEngine")
        .def(py::init<const std::string&, const std::string&, const std::string&,
                      const std::string&, const std::string&, const std::string&,
                      const std::string&, uint32_t>(),
             py::arg("graph_path"), py::arg("flags_path"),
             py::arg("snap_wave_path"), py::arg("snap_wind_path"),
             py::arg("cch_topo_path"), py::arg("weights_dir"),
             py::arg("avg_weather_dir") = "", py::arg("poll_interval_s") = 0,
             "weights_dir must contain weights.bin (written by weather_etl). "
             "avg_weather_dir is optional: a directory of <YYYY>/<MM>/<DD>/<field>.npy "
             "(the average-weather dataset) giving the weather sampled into the route "
             "result, for whichever date weights.bin's base_epoch carries; omit it to "
             "route on distance/weights alone (FOC totals and weather samples stay "
             "empty). poll_interval_s=0 (default) loads once and never re-checks "
             "weights_dir; pass a positive value to re-poll periodically "
             "like the long-running maritime-router-server.")
        .def_property_readonly("n_nodes", &PyRoutingEngine::n_nodes)
        .def_property_readonly("n_edges", &PyRoutingEngine::n_edges)
        .def("route", &PyRoutingEngine::route,
             py::arg("origin_lat"), py::arg("origin_lon"),
             py::arg("dest_lat"),   py::arg("dest_lon"),
             py::arg("service_speed_kts")   = 14.f,
             py::arg("draft_m")             = 10.f,
             py::arg("beam_m")              = 32.f,
             py::arg("loa_m")               = 200.f,
             py::arg("block_coeff")         = 0.80f,
             py::arg("displacement_t")      = 50000.f,
             py::arg("transverse_area_m2")  = 600.f,
             py::arg("base_time_step")      = 0,
             "Route from (origin_lat, origin_lon) to (dest_lat, dest_lon). "
             "Snaps each endpoint to its nearest graph node, then runs a CCH "
             "query against the currently loaded weights/weather.");

    py::class_<maritime::router_server::DaySegment>(m, "DaySegment")
        .def_readonly("day",     &maritime::router_server::DaySegment::day)
        .def_readonly("dist_nm", &maritime::router_server::DaySegment::dist_nm)
        .def_readonly("foc_mt",  &maritime::router_server::DaySegment::foc_mt)
        .def_readonly("time_h",  &maritime::router_server::DaySegment::time_h)
        .def_property_readonly("lat",
            [](const maritime::router_server::DaySegment& s) { return to_array(s.lat); })
        .def_property_readonly("lon",
            [](const maritime::router_server::DaySegment& s) { return to_array(s.lon); });

    py::class_<maritime::router_server::VoyagePlan>(m, "VoyagePlan")
        .def_readonly("segments",            &maritime::router_server::VoyagePlan::segments)
        .def_readonly("total_dist_nm",       &maritime::router_server::VoyagePlan::total_dist_nm)
        .def_readonly("total_foc_mt",        &maritime::router_server::VoyagePlan::total_foc_mt)
        .def_readonly("total_time_h",        &maritime::router_server::VoyagePlan::total_time_h)
        .def_readonly("reached_destination", &maritime::router_server::VoyagePlan::reached_destination);

    m.def("plan_voyage", &plan_voyage_py,
          py::arg("graph_path"), py::arg("flags_path"),
          py::arg("snap_wave_path"), py::arg("snap_wind_path"),
          py::arg("cch_topo_path"),
          py::arg("avg_weather_dir"), py::arg("start_date"),
          py::arg("origin_lat"), py::arg("origin_lon"),
          py::arg("dest_lat"),   py::arg("dest_lon"),
          py::arg("service_speed_kts")  = 14.f,
          py::arg("draft_m")            = 10.f,
          py::arg("beam_m")             = 32.f,
          py::arg("loa_m")              = 200.f,
          py::arg("block_coeff")        = 0.80f,
          py::arg("displacement_t")     = 50000.f,
          py::arg("transverse_area_m2") = 600.f,
          py::arg("sigma_along_nm")     = 200.f,
          py::arg("sigma_perp_nm")      = 400.f,
          py::arg("max_periods")        = 366,
          "Rolling-horizon multi-day voyage: for each ~24h period, computes "
          "that period's calendar date (start_date + days elapsed), loads "
          "its daily-average weather from avg_weather_dir (mapped onto the "
          "2024 dataset — see average_weather_description.md), re-customises "
          "the CCH, and advances the vessel day by day until the destination "
          "is reached or max_periods is exhausted. start_date is \"YYYY-MM-DD\". "
          "Same algorithm as the maritime-voyage-router CLI "
          "(router_server/src/voyage_planner.cpp) — this binding does not "
          "reimplement it, just calls it.");
}
