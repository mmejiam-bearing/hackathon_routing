//
// maritime-weights-writer
//
// Weather ETL entry point. Loads one calendar day's average weather
// (mapped onto the 2024 dataset — see average_weather_description.md) and a
// pre-built graph, then writes weights.bin for the router_server to consume.
//
// Usage:
//   maritime-weights-writer              \
//     --graph           graph.bin        \
//     --flags           flags.bin        \
//     --snap-wave       snap_wave.bin    \
//     --snap-wind       snap_wind.bin    \
//     --avg-weather-dir /path/to/output  \
//     --date            2026-06-08       \
//     --out             weights.bin      \
//     [--step  0]                (ref_time_step, default 0)
//     [--epoch <unix-seconds>]   (base_epoch for WeightsHeader; default:
//                                 midnight UTC of --date — QueryServer
//                                 derives which date's weather to display
//                                 from this field, so it must identify the
//                                 same date --date selected, not "now")
//
#include "avg_weather_loader.hpp"
#include "weights_writer.hpp"

#include "maritime/static_graph.hpp"
#include "maritime/weather_manager.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << "\n"
        << "  --graph           <path>   graph.bin\n"
        << "  --flags           <path>   flags.bin\n"
        << "  --snap-wave       <path>   snap_wave.bin\n"
        << "  --snap-wind       <path>   snap_wind.bin\n"
        << "  --avg-weather-dir <dir>    contains <YYYY>/<MM>/<DD>/<field>.npy\n"
        << "  --date            <YYYY-MM-DD>\n"
        << "  --out    <path>   output path for weights.bin\n"
        << "  --step   <int>    ref_time_step [0..23] (default 0)\n"
        << "  --epoch  <int>    Unix epoch identifying the weather's date "
           "(default: midnight UTC of --date)\n";
}

// Parses "YYYY-MM-DD" into (year, month, day). Throws on malformed input.
void parse_iso_date(const std::string& s, int& year, int& month, int& day)
{
    int y, m, d;
    char dash1, dash2;
    std::istringstream iss(s);
    iss >> y >> dash1 >> m >> dash2 >> d;
    if (!iss || dash1 != '-' || dash2 != '-')
        throw std::invalid_argument("--date must be YYYY-MM-DD, got: " + s);
    year = y; month = m; day = d;
}

struct Args {
    std::string graph_path;
    std::string flags_path;
    std::string snap_wave_path;
    std::string snap_wind_path;
    std::string avg_weather_dir;
    std::string date_str;
    std::string out_path;
    int         ref_step    = 0;
    int64_t     epoch       = 0;
    bool        epoch_given = false;
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

        if      (key == "--graph")           args.graph_path      = val;
        else if (key == "--flags")           args.flags_path      = val;
        else if (key == "--snap-wave")       args.snap_wave_path  = val;
        else if (key == "--snap-wind")       args.snap_wind_path  = val;
        else if (key == "--avg-weather-dir") args.avg_weather_dir = val;
        else if (key == "--date")            args.date_str        = val;
        else if (key == "--out")             args.out_path        = val;
        else if (key == "--step")            args.ref_step        = std::stoi(val);
        else if (key == "--epoch") {
            args.epoch       = static_cast<int64_t>(std::stoll(val));
            args.epoch_given = true;
        }
        else {
            std::cerr << "Unknown argument: " << key << "\n";
            print_usage(argv[0]);
            std::exit(EXIT_FAILURE);
        }
    }

    if (args.graph_path.empty()      || args.flags_path.empty() ||
        args.snap_wave_path.empty()  || args.snap_wind_path.empty() ||
        args.avg_weather_dir.empty() || args.date_str.empty() ||
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

        int year = 0, month = 0, day = 0;
        parse_iso_date(args.date_str, year, month, day);

        int64_t epoch = args.epoch;
        if (!args.epoch_given) {
            const std::chrono::sys_days midnight_utc{
                std::chrono::year{year} /
                std::chrono::month{static_cast<unsigned>(month)} /
                std::chrono::day{static_cast<unsigned>(day)}};
            epoch = std::chrono::duration_cast<std::chrono::seconds>(
                midnight_utc.time_since_epoch()).count();
        }

        std::cout << "[weights-writer] Loading graph from "
                  << args.graph_path << " ...\n";
        maritime::StaticGraph graph(
            args.graph_path, args.flags_path,
            args.snap_wave_path, args.snap_wind_path);

        std::cout << "[weights-writer] Loading average weather for "
                  << args.date_str << " from " << args.avg_weather_dir << " ...\n";
        auto buf = maritime::weather_etl::AvgWeatherLoader::load(
            args.avg_weather_dir, year, month, day);
        buf->base_epoch = epoch;

        std::cout << "[weights-writer] Computing edge weights "
                     "(ref_time_step=" << args.ref_step << ") ...\n";
        auto weights = maritime::weather_etl::WeightsWriter::compute(
            graph, *buf, args.ref_step);

        std::cout << "[weights-writer] Writing " << args.out_path << " ...\n";
        maritime::weather_etl::WeightsWriter::write(
            weights, epoch, args.out_path);

        std::cout << "[weights-writer] Done. " << weights.size()
                  << " edge weights written.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
