#include "cch_preprocessor.hpp"

#include <routingkit/nested_dissection.h>
#include <routingkit/vector_io.h>

#include <iostream>
#include <stdexcept>

namespace maritime::graph_builder {

CchPreprocessor::CchPreprocessor(
    uint32_t                     n_nodes,
    const std::vector<uint32_t>& tail,
    const std::vector<uint32_t>& head,
    const std::vector<float>&    lat,
    const std::vector<float>&    lon)
{
    order_ = RoutingKit::compute_nested_node_dissection_order_using_inertial_flow(
        static_cast<unsigned>(n_nodes),
        std::vector<unsigned>(tail.begin(), tail.end()),
        std::vector<unsigned>(head.begin(), head.end()),
        lat,
        lon,
        [](const std::string& msg) { std::cout << "      [cch] " << msg << "\n"; });
}

void CchPreprocessor::save(const std::string& out_path) const
{
    RoutingKit::save_vector(out_path, order_);
}

} // namespace maritime::graph_builder
