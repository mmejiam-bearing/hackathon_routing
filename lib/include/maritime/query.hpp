#pragma once

#include <cstdint>
#include <vector>

namespace maritime {

// Placeholder query-state type to keep the shared API layout stable.
struct QueryResult {
    std::vector<uint32_t> node_path;
    uint32_t total_weight = 0;
};

} // namespace maritime
