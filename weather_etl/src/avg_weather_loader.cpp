#include "avg_weather_loader.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace maritime::weather_etl {

namespace {

// ---------------------------------------------------------------------------
// Minimal .npy v1.0 header parser for the average-weather files: validates
// the magic, reads the declared shape out of the header dict string (e.g.
// "{'descr': '<f8', 'fortran_order': False, 'shape': (621, 1440), }"), and
// returns the byte offset of the first array element.
// ---------------------------------------------------------------------------
std::size_t parse_npy_header(std::ifstream& f, int expected_rows, int expected_cols,
                              const std::string& path)
{
    char magic[6];
    f.read(magic, 6);
    if (std::strncmp(magic, "\x93NUMPY", 6) != 0)
        throw std::runtime_error("AvgWeatherLoader: not a .npy file: " + path);

    uint8_t major, minor;
    f.read(reinterpret_cast<char*>(&major), 1);
    f.read(reinterpret_cast<char*>(&minor), 1);

    uint16_t header_len;
    f.read(reinterpret_cast<char*>(&header_len), 2);

    std::string header(header_len, '\0');
    f.read(header.data(), header_len);

    if (header.find("'descr': '<f8'") == std::string::npos)
        throw std::runtime_error("AvgWeatherLoader: expected float64: " + path);

    const auto shape_pos = header.find("'shape':");
    if (shape_pos == std::string::npos)
        throw std::runtime_error("AvgWeatherLoader: no shape in header: " + path);
    int rows = 0, cols = 0;
    if (std::sscanf(header.c_str() + shape_pos, "'shape': (%d, %d)", &rows, &cols) != 2)
        throw std::runtime_error("AvgWeatherLoader: cannot parse shape: " + path);

    if (rows != expected_rows || cols != expected_cols)
        throw std::runtime_error(
            "AvgWeatherLoader: shape mismatch in " + path +
            " — expected (" + std::to_string(expected_rows) + ", " +
            std::to_string(expected_cols) + "), got (" +
            std::to_string(rows) + ", " + std::to_string(cols) + ")");

    return static_cast<std::size_t>(10 + header_len);
}

// Reads one field's .npy file (float64, NaN = land) and narrows each value
// to _Float16, broadcasting the single (lat,lon) snapshot across all
// WX_N_TIMESTEPS hourly slots.
void load_field(const std::string& dir, const char* field,
                 int rows, int cols, std::vector<_Float16>& dst)
{
    const std::string path = dir + "/" + field + ".npy";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("AvgWeatherLoader: cannot open " + path);

    parse_npy_header(f, rows, cols, path);

    const std::size_t n_points = static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols);
    std::vector<double> raw(n_points);
    f.read(reinterpret_cast<char*>(raw.data()),
           static_cast<std::streamsize>(n_points * sizeof(double)));
    if (!f)
        throw std::runtime_error("AvgWeatherLoader: truncated read from " + path);

    for (std::size_t ts = 0; ts < static_cast<std::size_t>(WX_N_TIMESTEPS); ++ts) {
        _Float16* slot = dst.data() + ts * n_points;
        for (std::size_t i = 0; i < n_points; ++i)
            slot[i] = static_cast<_Float16>(raw[i]);
    }
}

} // namespace

std::shared_ptr<WeatherBuffer> AvgWeatherLoader::load(
    const std::string& base_dir, int /*year*/, int month, int day)
{
    // The average dataset covers 2024 only; every real calendar date maps
    // onto 2024's same month/day (2024 is a leap year, so Feb 29 resolves
    // too — no special casing needed).
    char mm[3], dd[3];
    std::snprintf(mm, sizeof(mm), "%02d", month);
    std::snprintf(dd, sizeof(dd), "%02d", day);
    const std::string dir = base_dir + "/2024/" + mm + "/" + dd;

    auto buf = WeatherBuffer::make_empty();

    load_field(dir, "sigwh",          WAVE_NJ, WX_NI, buf->sigwh);
    load_field(dir, "wsh",            WAVE_NJ, WX_NI, buf->wsh);
    load_field(dir, "wsp",            WAVE_NJ, WX_NI, buf->wsp);
    load_field(dir, "wsd",            WAVE_NJ, WX_NI, buf->wsd);
    load_field(dir, "pwd",            WAVE_NJ, WX_NI, buf->pwd);
    load_field(dir, "swell_residual", WAVE_NJ, WX_NI, buf->swell_residual);
    load_field(dir, "was",            WIND_NJ, WX_NI, buf->was);
    load_field(dir, "wad",            WIND_NJ, WX_NI, buf->wad);

    return buf;
}

} // namespace maritime::weather_etl
