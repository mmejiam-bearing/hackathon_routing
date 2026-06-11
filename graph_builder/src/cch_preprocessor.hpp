#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace maritime::graph_builder {

class CchPreprocessor {
public:
    CchPreprocessor(
        uint32_t                     n_nodes,
        const std::vector<uint32_t>& tail,
        const std::vector<uint32_t>& head,
        const std::vector<float>&    lat,
        const std::vector<float>&    lon);

    void save(const std::string& out_path) const;

private:
    std::vector<unsigned> order_;
};

} // namespace maritime::graph_builder
