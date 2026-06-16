//
// Python bindings for maritime::weather_etl.
//
// Exposes exactly the surface the ETL job needs: load a StaticGraph, load a
// calendar day's average-weather WeatherBuffer (AvgWeatherLoader, mapped
// onto the 2024 dataset), and call WeightsWriter::compute() /
// WeightsWriter::write(). All three are weather_etl's job per AGENTS.md —
// this module does not add any new computation, it only forwards to the
// existing C++ implementation so the Python ETL script can drop its
// pure-Python fallback (~18M Python-loop iterations, and a duplicate,
// drift-prone copy of the grid model) for the C++ path.
//
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include "avg_weather_loader.hpp"
#include "weights_writer.hpp"

#include "maritime/static_graph.hpp"
#include "maritime/weather_manager.hpp"

#include <memory>
#include <string>

namespace py = pybind11;

PYBIND11_MODULE(maritime_weather_etl, m)
{
    m.doc() = "Native bindings for maritime weather_etl: StaticGraph + WeightsWriter";

    py::class_<maritime::StaticGraph>(m, "StaticGraph")
        .def(py::init<const std::string&, const std::string&,
                      const std::string&, const std::string&>(),
             py::arg("graph_path"), py::arg("flags_path"),
             py::arg("snap_wave_path"), py::arg("snap_wind_path"))
        .def_property_readonly("n_nodes", &maritime::StaticGraph::n_nodes)
        .def_property_readonly("n_edges", &maritime::StaticGraph::n_edges);

    py::class_<maritime::WeatherBuffer, std::shared_ptr<maritime::WeatherBuffer>>(
        m, "WeatherBuffer")
        .def_readonly("base_epoch", &maritime::WeatherBuffer::base_epoch);

    m.def("load_avg_weather_buffer",
          [](const std::string& avg_weather_dir, int year, int month, int day,
             int64_t base_epoch) {
              auto buf = maritime::weather_etl::AvgWeatherLoader::load(
                  avg_weather_dir, year, month, day);
              buf->base_epoch = base_epoch;
              return buf;
          },
          py::arg("avg_weather_dir"), py::arg("year"), py::arg("month"), py::arg("day"),
          py::arg("base_epoch"),
          "Load one calendar day's average weather (sigwh, wsh, wsp, wsd, pwd, "
          "swell_residual, was, wad), mapped onto the 2024 dataset under "
          "avg_weather_dir/2024/<MM>/<DD>/<field>.npy — see "
          "average_weather_description.md.");

    py::class_<maritime::weather_etl::WeightsWriter>(m, "WeightsWriter")
        .def_static("compute",
            [](const maritime::StaticGraph& graph,
               const maritime::WeatherBuffer& wx,
               int ref_time_step) {
                const auto weights = maritime::weather_etl::WeightsWriter::compute(
                    graph, wx, ref_time_step, /*vessel=*/nullptr);
                return py::array_t<uint32_t>(
                    static_cast<py::ssize_t>(weights.size()), weights.data());
            },
            py::arg("graph"), py::arg("wx"), py::arg("ref_time_step"),
            "Vessel-agnostic proxy edge weights (distance x weather factor). "
            "Returns a uint32 numpy array, one entry per graph edge.")
        .def_static("write",
            [](py::array_t<uint32_t, py::array::c_style | py::array::forcecast> weights,
               int64_t base_epoch, const std::string& out_path) {
                const std::vector<uint32_t> w(
                    weights.data(), weights.data() + weights.size());
                maritime::weather_etl::WeightsWriter::write(w, base_epoch, out_path);
            },
            py::arg("weights"), py::arg("base_epoch"), py::arg("out_path"),
            "Write weights.bin matching WeightsHeader (lib/include/maritime/weights_header.hpp).");
}
