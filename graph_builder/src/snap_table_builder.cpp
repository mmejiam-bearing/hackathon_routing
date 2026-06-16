#include "snap_table_builder.hpp"

#include "maritime/static_graph.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <queue>
#include <stdexcept>
#include <vector>

namespace maritime::graph_builder {

// ---------------------------------------------------------------------------
// Numpy .npy v1.0 header parser — minimal, handles only what the mask
// .npy files need. Returns byte offset of the first array element.
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

    // Skip the header string — shape is supplied by the caller
    f.seekg(header_len, std::ios::cur);

    return static_cast<std::size_t>(10 + header_len);
}

// ---------------------------------------------------------------------------
// BFS-based nearest-ocean-cell for every grid point.
// This replicates scipy.ndimage.distance_transform_edt semantics on the
// ocean/land binary mask, using a multi-source BFS from all ocean seeds.
//
// Complexity: O(NJ * NI) — one pass over the grid.
// ---------------------------------------------------------------------------
SnapTable build_snap_table(const std::string& mask_npy_path, int nj, int ni)
{
    const int n = nj * ni;

    // Read the float64 ocean mask. NaN = land, finite = ocean.
    std::ifstream f(mask_npy_path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error(
            "snap_table_builder: cannot open " + mask_npy_path);

    parse_npy_header(f);

    std::vector<double> raw(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(raw.data()),
           static_cast<std::streamsize>(raw.size() * sizeof(double)));
    if (!f)
        throw std::runtime_error(
            "snap_table_builder: truncated read from " + mask_npy_path);

    // ocean[i] = true means cell i is a valid weather sample point
    std::vector<bool> ocean(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        ocean[static_cast<std::size_t>(i)] = !std::isnan(raw[static_cast<std::size_t>(i)]);

    // Multi-source BFS: all ocean cells are sources with distance 0.
    // For each cell we record the nearest ocean cell's (lat_i, lon_i).
    SnapTable result;
    result.snap_lat.resize(static_cast<std::size_t>(n), 0);
    result.snap_lon.resize(static_cast<std::size_t>(n), 0);

    std::vector<bool> visited(static_cast<std::size_t>(n), false);
    std::queue<int> q;   // queue entries: flat grid index

    for (int i = 0; i < n; ++i) {
        if (ocean[static_cast<std::size_t>(i)]) {
            const int lat_i = i / ni;
            const int lon_i = i % ni;
            result.snap_lat[static_cast<std::size_t>(i)] = static_cast<uint16_t>(lat_i);
            result.snap_lon[static_cast<std::size_t>(i)] = static_cast<uint16_t>(lon_i);
            visited[static_cast<std::size_t>(i)] = true;
            q.push(i);
        }
    }

    // 4-connected BFS (N/S/E/W); diagonal snap is conservative but safe
    constexpr int DR[4] = {-1, 1,  0, 0};
    constexpr int DC[4] = { 0, 0, -1, 1};

    while (!q.empty()) {
        const int cur = q.front();
        q.pop();

        const int r = cur / ni;
        const int c = cur % ni;

        for (int d = 0; d < 4; ++d) {
            const int nr = r + DR[d];
            const int nc = (c + DC[d] + ni) % ni;  // wrap longitude

            if (nr < 0 || nr >= nj) continue;

            const int nb = nr * ni + nc;
            if (visited[static_cast<std::size_t>(nb)]) continue;

            visited[static_cast<std::size_t>(nb)]         = true;
            result.snap_lat[static_cast<std::size_t>(nb)] = result.snap_lat[static_cast<std::size_t>(cur)];
            result.snap_lon[static_cast<std::size_t>(nb)] = result.snap_lon[static_cast<std::size_t>(cur)];
            q.push(nb);
        }
    }

    return result;
}

void serialise_snap_table(
    const SnapTable& table, int nj, int ni, const std::string& out_path)
{
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
        throw std::runtime_error(
            "serialise_snap_table: cannot open " + out_path);

    maritime::SnapHeader hdr{};
    hdr.magic   = 0x5041'4E53u;   // "SNAP" LE: S=0x53 N=0x4E A=0x41 P=0x50
    hdr.version = 1;
    hdr.n_lat   = static_cast<uint32_t>(nj);
    hdr.n_lon   = static_cast<uint32_t>(ni);
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

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
