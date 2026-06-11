#include "ocs_loader.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace maritime::weather_etl {

namespace {

// RTOFS grid constants (confirmed from S3 file inspection)
constexpr int OCS_NJ   = 1121;   // lat rows: 70°N → 70°S at 0.125°
constexpr int OCS_NI   = 2880;   // lon cols: 0° → 359.875° at 0.125°
constexpr float OCS_LAT_MAX  =  70.f;
constexpr float OCS_RES      =   0.125f;

// GFS grid constants (must match weather_manager.hpp)
constexpr int GFS_NJ   = 721;    // 90°N → 90°S at 0.25°
constexpr int GFS_NI   = 1440;   // 0° → 359.75° at 0.25°
constexpr float GFS_LAT_MAX  =  90.f;
constexpr float GFS_RES      =   0.25f;

// ---------------------------------------------------------------------------
// Minimal .npy v1.0 header skip — reads header_len and seeks past it.
// ---------------------------------------------------------------------------
static void skip_npy_header(std::ifstream& f, const std::string& path)
{
    char magic[6];
    f.read(magic, 6);
    if (std::strncmp(magic, "\x93NUMPY", 6) != 0)
        throw std::runtime_error("OcsLoader: not a .npy file: " + path);
    uint8_t major, minor;
    f.read(reinterpret_cast<char*>(&major), 1);
    f.read(reinterpret_cast<char*>(&minor), 1);
    uint16_t header_len;
    f.read(reinterpret_cast<char*>(&header_len), 2);
    f.seekg(header_len, std::ios::cur);
}

} // namespace

OcsLoader::UVPair OcsLoader::load(
    const std::string& ocs_path,
    const std::string& ocd_path)
{
    constexpr int OCS_N = OCS_NJ * OCS_NI;

    // ------------------------------------------------------------------
    // Load RTOFS speed (float16) and direction (int8) from the first copy
    // of each double-written .npy file.
    // ------------------------------------------------------------------
    std::vector<uint16_t> spd_raw(OCS_N);
    {
        std::ifstream f(ocs_path, std::ios::binary);
        if (!f.is_open())
            throw std::runtime_error("OcsLoader: cannot open " + ocs_path);
        skip_npy_header(f, ocs_path);
        f.read(reinterpret_cast<char*>(spd_raw.data()),
               static_cast<std::streamsize>(OCS_N * 2));
        if (!f)
            throw std::runtime_error(
                "OcsLoader: truncated read from " + ocs_path);
    }

    std::vector<int8_t> dir_raw(OCS_N);
    {
        std::ifstream f(ocd_path, std::ios::binary);
        if (!f.is_open())
            throw std::runtime_error("OcsLoader: cannot open " + ocd_path);
        skip_npy_header(f, ocd_path);
        f.read(reinterpret_cast<char*>(dir_raw.data()),
               static_cast<std::streamsize>(OCS_N));
        if (!f)
            throw std::runtime_error(
                "OcsLoader: truncated read from " + ocd_path);
    }

    // ------------------------------------------------------------------
    // Decompose speed + direction → U/V on the RTOFS grid.
    //
    // Direction encoding: value 1..16 = compass sector, each 22.5°.
    //   sector 1 = N (0°), sector 2 = NNE (22.5°), ..., sector 16 = NNW (337.5°)
    //   angle_deg = (sector - 1) * 22.5
    //
    // "going-to" convention (same as wad.npy):
    //   U = speed * sin(angle_rad)    (eastward)
    //   V = speed * cos(angle_rad)    (northward)
    //
    // -1 = land/missing → U = V = 0
    // ------------------------------------------------------------------
    constexpr float DEG2R = std::numbers::pi_v<float> / 180.f;

    std::vector<float> rtofs_u(OCS_N, 0.f);
    std::vector<float> rtofs_v(OCS_N, 0.f);

    for (int i = 0; i < OCS_N; ++i) {
        const int8_t sector = dir_raw[i];
        if (sector < 1 || sector > 16) continue;   // -1 = land/missing

        // float16 → float32 via memcpy to avoid strict-aliasing violation
        _Float16 spd16;
        std::memcpy(&spd16, &spd_raw[i], sizeof(uint16_t));
        const float spd = static_cast<float>(spd16);

        if (spd <= 0.f) continue;

        const float angle = static_cast<float>(sector - 1) * 22.5f * DEG2R;
        rtofs_u[i] = spd * std::sin(angle);
        rtofs_v[i] = spd * std::cos(angle);
    }

    // ------------------------------------------------------------------
    // Regrid RTOFS (1121×2880) → GFS (721×1440) by nearest-neighbour.
    //
    // GFS lat  at row r: lat = 90 - r * 0.25   (row 0 = 90°N)
    // RTOFS lat at row j: lat = 70 - j * 0.125  (row 0 = 70°N)
    //
    // GFS lon  at col c: lon = c * 0.25         (0 → 359.75)
    // RTOFS lon at col i: lon = i * 0.125        (0 → 359.875)
    //
    // GFS rows outside ±70° (r < 80 or r > 640) receive 0.
    // ------------------------------------------------------------------
    constexpr int GFS_N = GFS_NJ * GFS_NI;
    UVPair result;
    result.u.assign(GFS_N, 0.f);
    result.v.assign(GFS_N, 0.f);

    for (int r = 0; r < GFS_NJ; ++r) {
        const float lat = GFS_LAT_MAX - r * GFS_RES;
        if (lat > OCS_LAT_MAX || lat < -OCS_LAT_MAX) continue;

        const int j = static_cast<int>(
            std::round((OCS_LAT_MAX - lat) / OCS_RES));
        const int j_clamped = std::max(0, std::min(OCS_NJ - 1, j));

        for (int c = 0; c < GFS_NI; ++c) {
            const float lon = c * GFS_RES;   // 0 → 359.75°
            const int ci = static_cast<int>(
                std::round(lon / OCS_RES)) % OCS_NI;

            const int ocs_idx = j_clamped * OCS_NI + ci;
            const int gfs_idx = r * GFS_NI + c;
            result.u[gfs_idx] = rtofs_u[ocs_idx];
            result.v[gfs_idx] = rtofs_v[ocs_idx];
        }
    }

    return result;
}

} // namespace maritime::weather_etl
