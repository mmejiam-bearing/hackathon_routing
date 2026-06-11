//
// maritime-weights-writer
//
// Weather ETL entry point.  Loads a set of .npy weather files and a
// pre-built graph, then writes weights.bin for the router_server to consume.
//
// Usage:
//   maritime-weights-writer      \
//     --graph  graph.bin         \
//     --flags  flags.bin         \
//     --snap   snap.bin          \
//     --npy    /path/to/npy-dir  \
//     --out    weights.bin       \
//     [--step  0]                (ref_time_step, default 0)
//     [--epoch <unix-seconds>]   (base_epoch for WeightsHeader, default 0)
//
#include "npy_loader.hpp"
#include "ocs_loader.hpp"
#include "weights_writer.hpp"

#include "maritime/static_graph.hpp"
#include "maritime/weather_manager.hpp"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << "\n"
        << "  --graph  <path>   graph.bin\n"
        << "  --flags  <path>   flags.bin\n"
        << "  --snap   <path>   snap.bin\n"
        << "  --npy    <dir>    directory containing {var}.npy files\n"
        << "  --out    <path>   output path for weights.bin\n"
        << "  --step   <int>    ref_time_step [0..23] (default 0)\n"
        << "  --epoch  <int>    Unix epoch of forecast start (default: now)\n";
}

struct Args {
    std::string graph_path;
    std::string flags_path;
    std::string snap_path;
    std::string npy_dir;
    std::string out_path;
    int         ref_step  = 0;
    int64_t     epoch     = static_cast<int64_t>(std::time(nullptr));
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

        if      (key == "--graph")  args.graph_path = val;
        else if (key == "--flags")  args.flags_path = val;
        else if (key == "--snap")   args.snap_path  = val;
        else if (key == "--npy")    args.npy_dir    = val;
        else if (key == "--out")    args.out_path   = val;
        else if (key == "--step")   args.ref_step   = std::stoi(val);
        else if (key == "--epoch")  args.epoch      = static_cast<int64_t>(std::stoll(val));
        else {
            std::cerr << "Unknown argument: " << key << "\n";
            print_usage(argv[0]);
            std::exit(EXIT_FAILURE);
        }
    }

    if (args.graph_path.empty() || args.flags_path.empty() ||
        args.snap_path.empty()  || args.npy_dir.empty()    ||
        args.out_path.empty()) {
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

        std::cout << "[weights-writer] Loading graph from "
                  << args.graph_path << " ...\n";
        maritime::StaticGraph graph(
            args.graph_path, args.flags_path, args.snap_path);

        std::cout << "[weights-writer] Loading weather from "
                  << args.npy_dir << " ...\n";

        auto buf = maritime::WeatherBuffer::make_empty();
        buf->base_epoch = args.epoch;

        auto copy_npy = [&](std::vector<_Float16>& dst, const char* var) {
            auto raw = maritime::weather_etl::NpyLoader::load(
                args.npy_dir + "/" + var + ".npy");
            std::memcpy(dst.data(), raw.data(),
                        maritime::WX_N_POINTS * sizeof(uint16_t));
        };

        copy_npy(buf->sigwh, "sigwh");
        copy_npy(buf->pwh,   "pwh");
        copy_npy(buf->pwp,   "pwp");
        copy_npy(buf->pwd,   "pwd");
        copy_npy(buf->pswh,  "pswh");
        copy_npy(buf->pswp,  "pswp");
        copy_npy(buf->pswd,  "pswd");
        copy_npy(buf->wsh,   "wsh");
        copy_npy(buf->wsp,   "wsp");
        copy_npy(buf->was,   "was");
        copy_npy(buf->wad,   "wad");
        copy_npy(buf->wsd,   "wsd");

        auto uv = maritime::weather_etl::OcsLoader::load(
            args.npy_dir + "/ocs.npy",
            args.npy_dir + "/ocd.npy");
        for (std::size_t k = 0; k < static_cast<std::size_t>(maritime::WX_N_POINTS); ++k) {
            buf->ocs_u[k] = static_cast<_Float16>(uv.u[k]);
            buf->ocs_v[k] = static_cast<_Float16>(uv.v[k]);
        }

        std::cout << "[weights-writer] Computing edge weights "
                     "(ref_time_step=" << args.ref_step << ") ...\n";
        auto weights = maritime::weather_etl::WeightsWriter::compute(
            graph, *buf, args.ref_step);

        std::cout << "[weights-writer] Writing " << args.out_path << " ...\n";
        maritime::weather_etl::WeightsWriter::write(
            weights, args.epoch, args.out_path);

        std::cout << "[weights-writer] Done. " << weights.size()
                  << " edge weights written.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
