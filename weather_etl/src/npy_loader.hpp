#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::weather_etl {

class NpyLoader {
public:
    [[nodiscard]] static std::vector<uint16_t> load(const std::string& path);
};

} // namespace maritime::weather_etl
