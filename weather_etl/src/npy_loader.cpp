#include "npy_loader.hpp"

#include "maritime/weather_manager.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace maritime::weather_etl {

std::vector<uint16_t> NpyLoader::load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("NpyLoader: cannot open " + path);

    // Validate numpy magic
    char magic[6];
    f.read(magic, 6);
    if (std::strncmp(magic, "\x93NUMPY", 6) != 0)
        throw std::runtime_error("NpyLoader: not a .npy file: " + path);

    uint8_t  major, minor;
    uint16_t header_len;
    f.read(reinterpret_cast<char*>(&major), 1);
    f.read(reinterpret_cast<char*>(&minor), 1);
    f.read(reinterpret_cast<char*>(&header_len), 2);
    (void)major; (void)minor;

    // Skip header string — shape and dtype are known from the ETL contract
    f.seekg(header_len, std::ios::cur);

    // Read exactly WX_N_POINTS float16 elements.
    // The file contains a duplicate second copy (ETL double-write bug) —
    // we stop after the first copy.
    constexpr std::size_t N = static_cast<std::size_t>(maritime::WX_N_POINTS);
    std::vector<uint16_t> data(N);

    f.read(reinterpret_cast<char*>(data.data()),
           static_cast<std::streamsize>(N * sizeof(uint16_t)));

    if (!f)
        throw std::runtime_error(
            "NpyLoader: truncated read from " + path +
            " — expected " + std::to_string(N * 2) + " bytes");

    return data;
}

} // namespace maritime::weather_etl
