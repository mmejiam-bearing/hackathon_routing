//
// maritime-snap-builder
//
// Builds snap.bin from a single sigwh.npy file.
// Can run standalone — does not require GEBCO or GSHHG data.
//
// Usage:
//   maritime-snap-builder --sigwh /data/sigwh.npy --out /data/snap.bin
//
#include "snap_table_builder.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << "\n"
        << "  --sigwh  <path>   sigwh.npy (float16, shape 721x1440, NaN=land)\n"
        << "  --out    <path>   output snap.bin path\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 5) { print_usage(argv[0]); return EXIT_FAILURE; }

    std::string sigwh_path, out_path;
    for (int i = 1; i < argc - 1; i += 2) {
        std::string k{argv[i]}, v{argv[i+1]};
        if      (k == "--sigwh") sigwh_path = v;
        else if (k == "--out")   out_path   = v;
        else {
            std::cerr << "Unknown argument: " << k << "\n";
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (sigwh_path.empty() || out_path.empty()) {
        std::cerr << "Missing required arguments.\n";
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    try {
        std::cout << "[snap_builder] Reading " << sigwh_path << " ...\n";
        const auto table = maritime::graph_builder::build_snap_table(sigwh_path);

        std::cout << "[snap_builder] BFS complete: "
                  << table.snap_lat.size() << " cells\n";

        maritime::graph_builder::serialise_snap_table(table, out_path);
        std::cout << "[snap_builder] Written: " << out_path << "\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
