//
// maritime-snap-builder
//
// Builds snap_wave.bin and snap_wind.bin from one day's average-weather
// .npy files (sigwh.npy for the wave grid, was.npy for the wind grid).
// Can run standalone — does not require GEBCO or GSHHG data.
//
// Usage:
//   maritime-snap-builder --sigwh /data/sigwh.npy --was /data/was.npy \
//       --out-wave /data/snap_wave.bin --out-wind /data/snap_wind.bin
//
#include "snap_table_builder.hpp"

#include "maritime/weather_manager.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << "\n"
        << "  --sigwh    <path>   sigwh.npy (float64, shape 621x1440, NaN=land)\n"
        << "  --was      <path>   was.npy   (float64, shape 721x1440, NaN=land)\n"
        << "  --out-wave <path>   output snap_wave.bin path\n"
        << "  --out-wind <path>   output snap_wind.bin path\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 9) { print_usage(argv[0]); return EXIT_FAILURE; }

    std::string sigwh_path, was_path, out_wave_path, out_wind_path;
    for (int i = 1; i < argc - 1; i += 2) {
        std::string k{argv[i]}, v{argv[i+1]};
        if      (k == "--sigwh")    sigwh_path    = v;
        else if (k == "--was")      was_path      = v;
        else if (k == "--out-wave") out_wave_path = v;
        else if (k == "--out-wind") out_wind_path = v;
        else {
            std::cerr << "Unknown argument: " << k << "\n";
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (sigwh_path.empty() || was_path.empty() ||
        out_wave_path.empty() || out_wind_path.empty()) {
        std::cerr << "Missing required arguments.\n";
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    try {
        std::cout << "[snap_builder] Reading " << sigwh_path << " (wave grid) ...\n";
        const auto wave_table = maritime::graph_builder::build_snap_table(
            sigwh_path, maritime::WAVE_NJ, maritime::WX_NI);
        std::cout << "[snap_builder] Wave BFS complete: "
                  << wave_table.snap_lat.size() << " cells\n";
        maritime::graph_builder::serialise_snap_table(
            wave_table, maritime::WAVE_NJ, maritime::WX_NI, out_wave_path);
        std::cout << "[snap_builder] Written: " << out_wave_path << "\n";

        std::cout << "[snap_builder] Reading " << was_path << " (wind grid) ...\n";
        const auto wind_table = maritime::graph_builder::build_snap_table(
            was_path, maritime::WIND_NJ, maritime::WX_NI);
        std::cout << "[snap_builder] Wind BFS complete: "
                  << wind_table.snap_lat.size() << " cells\n";
        maritime::graph_builder::serialise_snap_table(
            wind_table, maritime::WIND_NJ, maritime::WX_NI, out_wind_path);
        std::cout << "[snap_builder] Written: " << out_wind_path << "\n";

        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
