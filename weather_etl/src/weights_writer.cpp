#include "weights_writer.hpp"

#include "maritime/edge_weight.hpp"
#include "maritime/foc_model.hpp"
#include "maritime/static_graph.hpp"

#include <routingkit/constants.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>

namespace maritime::weather_etl {

std::vector<uint32_t> WeightsWriter::compute(
    const maritime::StaticGraph&   graph,
    const maritime::WeatherBuffer& wx,
    int                            ref_time_step,
    const maritime::VesselParams*  vessel)
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

            constexpr float WIND_REF_SPEED = 20.f;   // normalise to gale-force (m/s)
            const uint32_t  v        = graph.col_idx()[e];
            const float     hdg      = bearing_rad(
                graph.lat()[u], graph.lon()[u],
                graph.lat()[v], graph.lon()[v]);
            const float wad_deg  = static_cast<float>(wx.wad[wx_idx]);
            const float was_val  = static_cast<float>(wx.was[wx_idx]);

            constexpr uint32_t MAX_W = RoutingKit::inf_weight - 1u;

            // ------------------------------------------------------------------
            // Physics-based branch: speed-loss time cost (consistent with
            // compute_edge_cost equal-power model).  Minimising time minimises
            // FOC when the engine runs at constant output.
            // ------------------------------------------------------------------
            if (vessel) {
                constexpr float DEG2R = std::numbers::pi_v<float> / 180.f;
                constexpr float R2DEG = 180.f / std::numbers::pi_v<float>;

                const float pwd_deg      = static_cast<float>(wx.pwd[wx_idx]);
                const float wind_from    = (wad_deg + 180.f) * DEG2R;
                const float wave_from    = (pwd_deg + 180.f) * DEG2R;
                const float wind_rel_deg = std::acos(std::cos(hdg - wind_from)) * R2DEG;
                const float wave_rel_deg = std::acos(std::cos(hdg - wave_from)) * R2DEG;

                const float R_cw   = maritime::calm_water_resistance_kn(
                                         vessel->service_speed_kts, *vessel);
                const float R_wave = maritime::wave_resistance_kn(
                                         sig_wh, wave_rel_deg, *vessel);
                const float R_wind = maritime::wind_resistance_kn(
                                         was_val, wind_rel_deg,
                                         vessel->service_speed_kts, *vessel);
                const float sl         = maritime::speed_loss_pct(R_cw, R_wave + R_wind);
                const float actual_spd = std::max(1.f,
                    vessel->service_speed_kts * (1.f - sl / 100.f));

                constexpr float SCALE = 1e6f;
                const float cost = base_nm / actual_spd;   // hours
                const uint32_t w = static_cast<uint32_t>(
                    std::min(cost * SCALE, static_cast<float>(MAX_W)));
                weights[e] = (w == 0u) ? 1u : w;
                continue;
            }

            // ------------------------------------------------------------------
            // Legacy proxy branch (vessel-agnostic, used by offline ETL).
            // ------------------------------------------------------------------
            const float wad_rad    = wad_deg * (std::numbers::pi_v<float> / 180.f);
            const float headwind_f = std::max(0.f, -was_val * std::cos(hdg - wad_rad))
                                     / WIND_REF_SPEED;

            const float proxy = base_nm * (1.f + sig_wh / 3.f + headwind_f);

            constexpr float    SCALE = 1e3f;
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
