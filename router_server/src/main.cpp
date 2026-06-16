//
// maritime-router-server
//
// Long-running query server.  Memory-maps graph artifacts at startup,
// polls weights.bin every --poll seconds, and triggers CCH metric
// customisation atomically when a new file is detected.
//
// Usage:
//   maritime-router-server \
//     --graph     /data/artifacts/graph.bin      \
//     --flags     /data/artifacts/flags.bin      \
//     --snap-wave /data/artifacts/snap_wave.bin  \
//     --snap-wind /data/artifacts/snap_wind.bin  \
//     --cch       /data/artifacts/cch_topo.bin   \
//     --weights   /data/weights/                 \
//     --avg-weather-dir /data/average-weather/    \
//     --poll      30
//
#include "server.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << "\n"
        << "  --graph     <path>   graph.bin (CSR graph + node coordinates)\n"
        << "  --flags     <path>   flags.bin (per-node bitmask)\n"
        << "  --snap-wave <path>   snap_wave.bin (wave-grid snap table)\n"
        << "  --snap-wind <path>   snap_wind.bin (wind-grid snap table)\n"
        << "  --cch       <path>   cch_topo.bin (CCH topology from graph_builder)\n"
        << "  --weights  <dir>    directory polled for weights.bin\n"
        << "  --avg-weather-dir <dir>  contains <YYYY>/<MM>/<DD>/<field>.npy\n"
        << "                           (optional; enables FOC; date comes from weights.bin's base_epoch)\n"
        << "  --poll     <secs>   poll interval in seconds (default: 30)\n";
}

struct Args {
    std::string graph_path;
    std::string flags_path;
    std::string snap_wave_path;
    std::string snap_wind_path;
    std::string cch_topo_path;
    std::string weights_dir;
    std::string avg_weather_dir;
    uint32_t    poll_interval_s = 30;
};

[[nodiscard]] Args parse_args(int argc, char** argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        std::exit(EXIT_FAILURE);
    }

    Args args;
    for (int i = 1; i < argc - 1; i += 2) {
        std::string key{argv[i]};
        std::string val{argv[i + 1]};

        if      (key == "--graph")     args.graph_path      = val;
        else if (key == "--flags")     args.flags_path      = val;
        else if (key == "--snap-wave") args.snap_wave_path  = val;
        else if (key == "--snap-wind") args.snap_wind_path  = val;
        else if (key == "--cch")       args.cch_topo_path   = val;
        else if (key == "--weights")          args.weights_dir     = val;
        else if (key == "--avg-weather-dir")  args.avg_weather_dir = val;
        else if (key == "--poll")     args.poll_interval_s = static_cast<uint32_t>(std::stoul(val));
        else {
            std::cerr << "Unknown argument: " << key << "\n";
            print_usage(argv[0]);
            std::exit(EXIT_FAILURE);
        }
    }

    if (args.graph_path.empty()     || args.flags_path.empty() ||
        args.snap_wave_path.empty() || args.snap_wind_path.empty() ||
        args.cch_topo_path.empty()  || args.weights_dir.empty()) {
        std::cerr << "Missing required arguments.\n";
        print_usage(argv[0]);
        std::exit(EXIT_FAILURE);
    }

    return args;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto args = parse_args(argc, argv);

        maritime::router_server::ServerConfig cfg{
            .graph_path      = args.graph_path,
            .flags_path      = args.flags_path,
            .snap_wave_path  = args.snap_wave_path,
            .snap_wind_path  = args.snap_wind_path,
            .cch_topo_path   = args.cch_topo_path,
            .weights_dir     = args.weights_dir,
            .avg_weather_dir = args.avg_weather_dir,
            .poll_interval_s = args.poll_interval_s,
        };

        maritime::router_server::QueryServer server(cfg);

        std::cout << "[router_server] Ready. Press Enter to stop.\n";
        std::cin.get();

        server.stop();
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
