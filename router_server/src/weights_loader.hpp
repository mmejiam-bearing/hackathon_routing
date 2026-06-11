#pragma once

#include "maritime/weights_header.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::router_server {

struct WeightsPayload {
    std::vector<uint32_t> weights;
    int64_t base_epoch = 0;
};

class WeightsLoader {
public:
    [[nodiscard]] static WeightsPayload load(const std::string& path);
};

} // namespace maritime::router_server
