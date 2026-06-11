#include "server.hpp"

#include "npy_loader.hpp"
#include "ocs_loader.hpp"

#include "maritime/weather_manager.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace maritime::router_server {

namespace fs = std::filesystem;

namespace {

// Load all 14 weather variables from the given directory and return a fully
// populated WeatherBuffer.  Only timestep 0 is filled (one .npy file = one
// spatial frame).  Timesteps 1–23 are left zero (calm-water fallback for
// longer routes in the POC).
std::shared_ptr<maritime::WeatherBuffer>
load_weather_buffer(const std::string& npy_dir, int64_t base_epoch)
{
    auto buf = maritime::WeatherBuffer::make_empty();
    buf->base_epoch = base_epoch;

    auto copy_npy = [&](std::vector<_Float16>& dst, const char* var) {
        auto raw = maritime::weather_etl::NpyLoader::load(
            npy_dir + "/" + var + ".npy");
        std::memcpy(dst.data(), raw.data(),
                    maritime::WX_N_POINTS * sizeof(uint16_t));
    };

    copy_npy(buf->sigwh, "sigwh");
    copy_npy(buf->pwh,   "pwh");
    copy_npy(buf->pwp,   "pwp");
    copy_npy(buf->pwd,   "pwd");
    copy_npy(buf->pswh,  "pswh");
    copy_npy(buf->pswp,  "pswp");
    copy_npy(buf->pswd,  "pswd");
    copy_npy(buf->wsh,   "wsh");
    copy_npy(buf->wsp,   "wsp");
    copy_npy(buf->was,   "was");
    copy_npy(buf->wad,   "wad");
    copy_npy(buf->wsd,   "wsd");

    auto uv = maritime::weather_etl::OcsLoader::load(
        npy_dir + "/ocs.npy",
        npy_dir + "/ocd.npy");
    for (std::size_t k = 0; k < static_cast<std::size_t>(maritime::WX_N_POINTS); ++k) {
        buf->ocs_u[k] = static_cast<_Float16>(uv.u[k]);
        buf->ocs_v[k] = static_cast<_Float16>(uv.v[k]);
    }

    return buf;
}

} // namespace

QueryServer::QueryServer(const ServerConfig& cfg)
    : engine_(cfg.graph_path, cfg.flags_path, cfg.snap_path, cfg.cch_topo_path)
    , cfg_(cfg)
{
    // Load initial weights if already present
    const std::string weights_path = cfg_.weights_dir + "/weights.bin";
    if (fs::exists(weights_path)) {
        auto payload = WeightsLoader::load(weights_path);
        engine_.update_weights(std::move(payload.weights), payload.base_epoch);
        last_weights_mtime_ = fs::last_write_time(weights_path);
        std::cout << "[router_server] Initial weights loaded from "
                  << weights_path << "\n";

        if (!cfg_.npy_dir.empty()) {
            engine_.update_weather(load_weather_buffer(cfg_.npy_dir, payload.base_epoch));
            std::cout << "[router_server] Initial WeatherBuffer loaded from "
                      << cfg_.npy_dir << "\n";
        }
    } else {
        std::cout << "[router_server] No weights.bin found at startup — "
                     "waiting for ETL\n";
    }

    // Start the background polling thread
    poll_thread_ = std::thread(&QueryServer::poll_loop, this);
}

QueryServer::~QueryServer()
{
    stop();
    if (poll_thread_.joinable())
        poll_thread_.join();
}

void QueryServer::stop() noexcept
{
    running_.store(false, std::memory_order_release);
}

CchRouteResult QueryServer::serve_query(const CchRouteRequest& req)
{
    return engine_.route(req);
}

void QueryServer::poll_loop()
{
    const std::string weights_path = cfg_.weights_dir + "/weights.bin";

    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(
            std::chrono::seconds(cfg_.poll_interval_s));

        if (!running_.load(std::memory_order_acquire)) break;

        try {
            if (!fs::exists(weights_path)) continue;

            const auto mtime = fs::last_write_time(weights_path);
            if (mtime <= last_weights_mtime_) continue;

            // New weights file detected — load and swap
            auto payload = WeightsLoader::load(weights_path);
            engine_.update_weights(
                std::move(payload.weights), payload.base_epoch);
            last_weights_mtime_ = mtime;

            std::cout << "[router_server] Weather weights updated from "
                      << weights_path << "\n";

            if (!cfg_.npy_dir.empty()) {
                engine_.update_weather(
                    load_weather_buffer(cfg_.npy_dir, payload.base_epoch));
                std::cout << "[router_server] WeatherBuffer updated from "
                          << cfg_.npy_dir << "\n";
            }
        }
        catch (const std::exception& e) {
            // Log but do not terminate — keep serving on old weights
            std::cerr << "[router_server] weights poll error: "
                      << e.what() << "\n";
        }
    }
}

} // namespace maritime::router_server
