#include "weights_loader.hpp"

#include <fstream>
#include <stdexcept>

namespace maritime::router_server {

WeightsPayload WeightsLoader::load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("WeightsLoader: cannot open " + path);

    maritime::WeightsHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f)
        throw std::runtime_error("WeightsLoader: truncated header in " + path);

    if (hdr.magic != 0x5448'4757u)   // "WGHT" LE
        throw std::runtime_error("WeightsLoader: bad magic in " + path);

    if (hdr.version != 1)
        throw std::runtime_error("WeightsLoader: unsupported version in " + path);

    WeightsPayload payload;
    payload.base_epoch = hdr.base_epoch;
    payload.weights.resize(hdr.n_edges);

    f.read(reinterpret_cast<char*>(payload.weights.data()),
           static_cast<std::streamsize>(hdr.n_edges * sizeof(uint32_t)));

    if (!f)
        throw std::runtime_error("WeightsLoader: truncated weights in " + path);

    return payload;
}

} // namespace maritime::router_server
