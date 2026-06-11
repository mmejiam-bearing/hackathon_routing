#include "weights_writer.hpp"

#include "maritime/static_graph.hpp"

#include <routingkit/constants.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace maritime::weather_etl {

std::vector<uint32_t> WeightsWriter::compute(
    const maritime::StaticGraph&   graph,
    const maritime::WeatherBuffer& wx,
    int                            ref_time_step)
{
    const uint32_t n_edges = graph.n_edges();
    std::vector<uint32_t> weights(n_edges, 1u);

    for (uint32_t u = 0; u < graph.n_nodes(); ++u) {
        for (uint32_t e = graph.row_ptr()[u]; e < graph.row_ptr()[u + 1]; ++e) {
            // Restricted nodes are impassable — store RoutingKit's sentinel
            // so the CCH treats them as disconnected.
            if (graph.flags()[u] & FLAG_RESTRICTED) {
                weights[e] = RoutingKit::inf_weight;
                continue;
            }

            const std::size_t wx_idx = graph.weather_idx(u, ref_time_step);

            const float base_nm = graph.base_dist()[e];
            const float sig_wh  = static_cast<float>(wx.sigwh[wx_idx]);

            // Wave-height proxy: distance scaled by sea state factor.
            const float proxy = base_nm * (1.f + sig_wh / 6.f);

            constexpr float    SCALE = 1e3f;
            constexpr uint32_t MAX_W = RoutingKit::inf_weight - 1u;
            const uint32_t     w     = static_cast<uint32_t>(
                std::min(proxy * SCALE, static_cast<float>(MAX_W)));

            weights[e] = (w == 0u) ? 1u : w;  // RoutingKit disallows zero weights
        }
    }

    return weights;
}

void WeightsWriter::write(
    const std::vector<uint32_t>& weights,
    int64_t                      base_epoch,
    const std::string&           out_path)
{
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
        throw std::runtime_error("WeightsWriter::write: cannot open " + out_path);

    WeightsHeader hdr{};
    hdr.magic      = 0x5448'4757u;  // "WGHT" LE: W=0x57 G=0x47 H=0x48 T=0x54
    hdr.version    = 1;
    hdr.n_edges    = static_cast<uint32_t>(weights.size());
    hdr.reserved   = 0;
    hdr.base_epoch = base_epoch;

    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(weights.data()),
            static_cast<std::streamsize>(weights.size() * sizeof(uint32_t)));

    if (!f)
        throw std::runtime_error(
            "WeightsWriter::write: write failed to " + out_path);
}

} // namespace maritime::weather_etl
