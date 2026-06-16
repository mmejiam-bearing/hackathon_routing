//
// maritime-graph-builder
//
// Offline pipeline — run once when data sources update (months between runs).
// Produces three binary artifacts consumed by the router server:
//
//   graph.bin       — CSR graph with node lat/lon/depth and edge distances
//   flags.bin       — per-node flag bitmask (ECA, TSS, canal, etc.)
//   snap_wave.bin   — wave-grid snap table (nearest ocean cell per grid point)
//   snap_wind.bin   — wind-grid snap table (nearest ocean cell per grid point)
//
// CCH topology is built here and serialized via RoutingKit's save() API so
// the router server can load it in milliseconds rather than rebuilding from
// scratch on every process start.
//
// Usage:
//   maritime-graph-builder \
//     --gebco    /data/gebco_2023.nc      \
//     --gshhg    /data/gshhg/             \
//     --enc      /data/noaa_enc/          \
//     --sigwh    /data/weather/sigwh.npy  \
//     --was      /data/weather/was.npy    \
//     --out      /data/artifacts/         \
//     [--no-restrictions]
//
#include "graph_builder.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << "\n"
        << "  --gebco           <path>   GEBCO 2023 NetCDF bathymetry file\n"
        << "  --gshhg           <path>   GSHHG directory (contains *.shp files)\n"
        << "  --enc             <path>   NOAA ENC directory (contains *.000 files)\n"
        << "  --sigwh           <path>   sigwh.npy — wave-grid land/ocean mask\n"
        << "  --was             <path>   was.npy — wind-grid land/ocean mask\n"
        << "  --out             <path>   output directory for artifacts\n"
        << "  --res             <float>  graph resolution in degrees (default: 0.25)\n"
        << "  --draft           <float>  minimum vessel draft filter in metres (default: 3.0)\n"
        << "  --no-restrictions         disable default geographic passage restrictions\n";
}

struct Args {
    std::string gebco_path;
    std::string gshhg_path;
    std::string enc_path;
    std::string sigwh_path;
    std::string was_path;
    std::string out_path;
    float       resolution_deg  = 0.25f;
    float       min_draft_m     = 3.0f;
    bool        no_restrictions = false;
};

[[nodiscard]] Args parse_args(int argc, char** argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        std::exit(EXIT_FAILURE);
    }

    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string key{argv[i]};

        if (key == "--no-restrictions") {
            args.no_restrictions = true;
            continue;
        }

        // All other flags take a value argument
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << key << "\n";
            print_usage(argv[0]);
            std::exit(EXIT_FAILURE);
        }
        std::string val{argv[++i]};

        if      (key == "--gebco")  args.gebco_path    = val;
        else if (key == "--gshhg")  args.gshhg_path    = val;
        else if (key == "--enc")    args.enc_path       = val;
        else if (key == "--sigwh")  args.sigwh_path     = val;
        else if (key == "--was")    args.was_path       = val;
        else if (key == "--out")    args.out_path       = val;
        else if (key == "--res")    args.resolution_deg = std::stof(val);
        else if (key == "--draft")  args.min_draft_m    = std::stof(val);
        else {
            std::cerr << "Unknown argument: " << key << "\n";
            print_usage(argv[0]);
            std::exit(EXIT_FAILURE);
        }
    }

    if (args.gebco_path.empty() || args.gshhg_path.empty() ||
        args.sigwh_path.empty() || args.was_path.empty() || args.out_path.empty()) {
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

        maritime::graph_builder::BuildConfig cfg{
            .gebco_path     = args.gebco_path,
            .gshhg_path     = args.gshhg_path,
            .enc_path       = args.enc_path,
            .sigwh_npy_path = args.sigwh_path,
            .was_npy_path   = args.was_path,
            .output_dir     = args.out_path,
            .resolution_deg = args.resolution_deg,
            .min_draft_m    = args.min_draft_m,
        };

        if (args.no_restrictions) {
            cfg.restricted_zones.clear();
            std::cout << "[graph-builder] Geographic restrictions disabled.\n";
        }

        maritime::graph_builder::run(cfg);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
