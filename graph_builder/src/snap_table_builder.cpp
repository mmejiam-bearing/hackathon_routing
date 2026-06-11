#include "snap_table_builder.hpp"

#include "maritime/static_graph.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <vector>

namespace maritime::graph_builder {

// ---------------------------------------------------------------------------
// Numpy .npy v1.0 header parser — minimal, handles only what sigwh.npy needs.
// Returns byte offset of the first array element.
// ---------------------------------------------------------------------------
static std::size_t parse_npy_header(std::ifstream& f)
{
    // Magic: \x93NUMPY + version (2 bytes) + header_len (2 bytes LE)
    char magic[6];
    f.read(magic, 6);
    if (std::strncmp(magic, "\x93NUMPY", 6) != 0)
        throw std::runtime_error("snap_table_builder: not a .npy file");

    uint8_t major, minor;
    f.read(reinterpret_cast<char*>(&major), 1);
    f.read(reinterpret_cast<char*>(&minor), 1);

    uint16_t header_len;
    f.read(reinterpret_cast<char*>(&header_len), 2);

    // Skip the header string — we already know the shape from the constants
    f.seekg(header_len, std::ios::cur);

    return static_cast<std::size_t>(10 + header_len);
}

// ---------------------------------------------------------------------------
// BFS-based nearest-ocean-cell for every grid point.
// This replicates scipy.ndimage.distance_transform_edt semantics on the
// ocean/land binary mask, using a multi-source BFS from all ocean seeds.
//
// Complexity: O(NJ * NI) — one pass over the 721 × 1440 grid.
// ---------------------------------------------------------------------------
[[nodiscard]] SnapTable build_snap_table(const std::string& sigwh_npy_path)
{
    constexpr int NJ = 721;
    constexpr int NI = 1440;
    constexpr int N  = NJ * NI;

    // Read the float16 ocean mask from sigwh.npy.
    // NaN = land, finite = ocean.
    std::ifstream f(sigwh_npy_path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error(
            "snap_table_builder: cannot open " + sigwh_npy_path);

    parse_npy_header(f);

    // Read first N elements only (the file contains a duplicate second copy)
    std::vector<uint16_t> raw(N);
    f.read(reinterpret_cast<char*>(raw.data()),
           static_cast<std::streamsize>(N * sizeof(uint16_t)));
    if (!f)
        throw std::runtime_error(
            "snap_table_builder: truncated read from " + sigwh_npy_path);

    // float16 NaN: exponent bits all 1 (0x7C00) and non-zero mantissa
    auto is_nan_f16 = [](uint16_t bits) -> bool {
        return (bits & 0x7C00u) == 0x7C00u && (bits & 0x03FFu) != 0u;
    };

    // ocean[i] = true means cell i is a valid weather sample point
    std::vector<bool> ocean(N);
    for (int i = 0; i < N; ++i)
        ocean[i] = !is_nan_f16(raw[i]);

    // Multi-source BFS: all ocean cells are sources with distance 0.
    // For each cell we record the nearest ocean cell's (lat_i, lon_i).
    SnapTable result;
    result.snap_lat.resize(N, 0);
    result.snap_lon.resize(N, 0);

    std::vector<bool> visited(N, false);
    // queue entries: flat grid index
    std::queue<int> q;

    for (int i = 0; i < N; ++i) {
        if (ocean[i]) {
            const int lat_i = i / NI;
            const int lon_i = i % NI;
            result.snap_lat[i] = static_cast<uint16_t>(lat_i);
            result.snap_lon[i] = static_cast<uint16_t>(lon_i);
            visited[i] = true;
            q.push(i);
        }
    }

    // 4-connected BFS (N/S/E/W); diagonal snap is conservative but safe
    constexpr int DR[4] = {-1, 1,  0, 0};
    constexpr int DC[4] = { 0, 0, -1, 1};

    while (!q.empty()) {
        const int cur = q.front();
        q.pop();

        const int r = cur / NI;
        const int c = cur % NI;

        for (int d = 0; d < 4; ++d) {
            const int nr = r + DR[d];
            const int nc = (c + DC[d] + NI) % NI;  // wrap longitude

            if (nr < 0 || nr >= NJ) continue;

            const int nb = nr * NI + nc;
            if (visited[nb]) continue;

            visited[nb]          = true;
            result.snap_lat[nb]  = result.snap_lat[cur];
            result.snap_lon[nb]  = result.snap_lon[cur];
            q.push(nb);
        }
    }

    return result;
}

void serialise_snap_table(const SnapTable& table, const std::string& out_path)
{
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
        throw std::runtime_error(
            "serialise_snap_table: cannot open " + out_path);

    // Write SnapHeader
    maritime::SnapHeader hdr{};
    hdr.magic   = 0x5041'4E53u;   // "SNAP" LE: S=0x53 N=0x4E A=0x41 P=0x50
    hdr.version = 1;
    hdr.n_lat   = 721;
    hdr.n_lon   = 1440;
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    // Write snap_lat then snap_lon
    const auto n = table.snap_lat.size();
    f.write(reinterpret_cast<const char*>(table.snap_lat.data()),
            static_cast<std::streamsize>(n * sizeof(uint16_t)));
    f.write(reinterpret_cast<const char*>(table.snap_lon.data()),
            static_cast<std::streamsize>(n * sizeof(uint16_t)));

    if (!f)
        throw std::runtime_error(
            "serialise_snap_table: write failed to " + out_path);
}

} // namespace maritime::graph_builder
