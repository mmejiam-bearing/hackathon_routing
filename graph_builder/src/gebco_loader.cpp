#include "gebco_loader.hpp"

#include <netcdf.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace maritime::graph_builder {

namespace {

constexpr int   GRAPH_NJ  = 721;
constexpr int   GRAPH_NI  = 1440;
constexpr float GRAPH_RES = 0.25f;

void nc_check(int status, const char* msg)
{
    if (status != NC_NOERR)
        throw std::runtime_error(std::string(msg) + ": " + nc_strerror(status));
}

double read_coord(int ncid, int varid, std::size_t idx)
{
    double val;
    std::size_t start = idx, count = 1;
    nc_check(nc_get_vara_double(ncid, varid, &start, &count, &val),
             "nc_get_vara_double");
    return val;
}

float to_depth(float z, float fill_val) noexcept
{
    // "positive up": negative z = ocean (below sea level) → positive depth.
    return (z == fill_val || z >= 0.f) ? 0.f : -z;
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor — load and subsample the bathymetry grid at construction time.
//
// Strategy (avoids negative HDF5 strides, which are unsupported on DEFLATE
// variables in the macOS netCDF-4 library):
//
//   1. Determine lat orientation from the coordinate array (ascending or
//      descending).  ETOPO stores lat ascending (−90→+90); GEBCO is also
//      typically ascending.
//
//   2. For ascending lat:
//        - Bulk read: 720 rows starting from the row nearest −90°S, stride
//          +abs_stride_j, stored *reversed* into depth_grid_[720..1].
//        - Extra read: 1 row at the row nearest +90°N → depth_grid_[0].
//
//   3. For descending lat (future-proof):
//        - Bulk read: 720 rows from the row nearest +90°N, stride
//          +abs_stride_j, stored *directly* into depth_grid_[0..719].
//        - Extra read: 1 row at the row nearest −90°S → depth_grid_[720].
//
//   This gives exactly two nc_get_vars_float calls, each with a positive
//   stride, so HDF5 decompresses each chunk at most once.
// ---------------------------------------------------------------------------
GebcoLoader::GebcoLoader(const std::string& path)
{
    // 256 MB chunk cache so DEFLATE chunks (1350×2700 ≈ 14.6 MB each) are
    // decompressed once and served from memory on repeated row accesses.
    nc_set_chunk_cache(256 * 1024 * 1024, 1009, 0.75);

    int ncid;
    nc_check(nc_open(path.c_str(), NC_NOWRITE, &ncid), "nc_open");

    int varid = -1;
    if (nc_inq_varid(ncid, "z", &varid) != NC_NOERR) {
        if (nc_inq_varid(ncid, "elevation", &varid) != NC_NOERR) {
            nc_close(ncid);
            throw std::runtime_error(
                "GebcoLoader: no 'z' or 'elevation' variable in " + path);
        }
    }

    int dimids[2];
    nc_check(nc_inq_vardimid(ncid, varid, dimids), "nc_inq_vardimid");
    std::size_t nrows, ncols;
    nc_check(nc_inq_dimlen(ncid, dimids[0], &nrows), "nc_inq_dimlen lat");
    nc_check(nc_inq_dimlen(ncid, dimids[1], &ncols), "nc_inq_dimlen lon");

    int lat_varid, lon_varid;
    nc_check(nc_inq_varid(ncid, "lat", &lat_varid), "nc_inq_varid lat");
    nc_check(nc_inq_varid(ncid, "lon", &lon_varid), "nc_inq_varid lon");

    const double lat0     = read_coord(ncid, lat_varid, 0);
    const double lat_step = read_coord(ncid, lat_varid, 1) - lat0;  // signed
    const double lon0     = read_coord(ncid, lon_varid, 0);
    const double lon_step = read_coord(ncid, lon_varid, 1) - lon0;  // positive

    float fill_val = -99999.f;
    nc_get_att_float(ncid, varid, "_FillValue", &fill_val);

    const bool lat_ascending = (lat_step > 0.0);

    // Row nearest +90°N and row nearest −90°S (both clamped to valid range).
    const auto j_north = static_cast<std::size_t>(std::clamp(
        std::llround((90.0 - lat0) / lat_step),
        0LL, static_cast<long long>(nrows - 1)));
    const auto j_south = static_cast<std::size_t>(std::clamp(
        std::llround((-90.0 - lat0) / lat_step),
        0LL, static_cast<long long>(nrows - 1)));

    // Col nearest −180°W (longitude origin used for depth_grid_ col 0).
    const auto k0 = static_cast<std::size_t>(std::clamp(
        std::llround((-180.0 - lon0) / lon_step),
        0LL, static_cast<long long>(ncols - 1)));

    // Strides (always positive — no negative HDF5 strides).
    const auto abs_stride_j = static_cast<ptrdiff_t>(
        std::llround(GRAPH_RES / std::abs(lat_step)));
    const auto stride_k = static_cast<ptrdiff_t>(
        std::llround(GRAPH_RES / lon_step));

    depth_grid_.resize(static_cast<std::size_t>(GRAPH_NJ) * GRAPH_NI, 0.f);

    // ------------------------------------------------------------------
    // Bulk read: 720 rows (graph rows 0..719 or 1..720 depending on orientation)
    // ------------------------------------------------------------------
    constexpr int ROWS_STRIDED = GRAPH_NJ - 1;  // 720

    // For ascending lat, start at the south-pole row and walk northward.
    // The resulting buf is in south→north order, so we store it reversed.
    // For descending lat, start at the north-pole row and walk southward.
    const std::size_t j_bulk_start = lat_ascending ? j_south : j_north;

    {
        std::vector<float> buf(static_cast<std::size_t>(ROWS_STRIDED) * GRAPH_NI);

        const std::size_t start[2]  = {j_bulk_start, k0};
        const std::size_t count[2]  = {static_cast<std::size_t>(ROWS_STRIDED),
                                       static_cast<std::size_t>(GRAPH_NI)};
        const ptrdiff_t   stride[2] = {abs_stride_j, stride_k};

        nc_check(nc_get_vars_float(ncid, varid, start, count, stride, buf.data()),
                 "nc_get_vars_float bulk");

        for (int i = 0; i < ROWS_STRIDED; ++i) {
            // ascending:  buf[0]=south → depth_grid_[720]; buf[719]=near-north → depth_grid_[1]
            // descending: buf[0]=north → depth_grid_[0];   buf[719]=near-south → depth_grid_[719]
            const int r = lat_ascending ? (ROWS_STRIDED - i) : i;
            const std::size_t dst_base = static_cast<std::size_t>(r) * GRAPH_NI;
            const std::size_t src_base = static_cast<std::size_t>(i) * GRAPH_NI;
            for (int c = 0; c < GRAPH_NI; ++c)
                depth_grid_[dst_base + c] = to_depth(buf[src_base + c], fill_val);
        }
    }

    // ------------------------------------------------------------------
    // Extra row: the pole row that the bulk read could not cover without
    // a negative stride.
    //   ascending:  graph row 0 (90°N) from j_north
    //   descending: graph row 720 (−90°S) from j_south
    // ------------------------------------------------------------------
    {
        const std::size_t j_extra = lat_ascending ? j_north : j_south;
        const int         r_extra = lat_ascending ? 0 : GRAPH_NJ - 1;

        std::vector<float> extra_row(GRAPH_NI);
        const std::size_t start[2]  = {j_extra, k0};
        const std::size_t count[2]  = {1, static_cast<std::size_t>(GRAPH_NI)};
        const ptrdiff_t   stride[2] = {1, stride_k};

        nc_check(nc_get_vars_float(ncid, varid, start, count, stride, extra_row.data()),
                 "nc_get_vars_float extra row");

        const std::size_t base = static_cast<std::size_t>(r_extra) * GRAPH_NI;
        for (int c = 0; c < GRAPH_NI; ++c)
            depth_grid_[base + c] = to_depth(extra_row[c], fill_val);
    }

    nc_close(ncid);
}

float GebcoLoader::depth_at(float lat, float lon) const noexcept
{
    const int r = std::clamp(
        static_cast<int>(std::round((90.f - lat) / GRAPH_RES)), 0, GRAPH_NJ - 1);
    const int c = std::clamp(
        static_cast<int>(std::round((lon + 180.f) / GRAPH_RES)), 0, GRAPH_NI - 1);
    return depth_grid_[static_cast<std::size_t>(r) * GRAPH_NI + c];
}

} // namespace maritime::graph_builder
