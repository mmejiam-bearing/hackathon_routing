#include "server.hpp"

#include "avg_weather_loader.hpp"

#include "maritime/weather_manager.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace maritime::router_server {

namespace fs = std::filesystem;

namespace {

// Loads the average weather (see average_weather_description.md) for the
// calendar date carried by weights.bin's base_epoch, mapped onto the 2024
// dataset. Using base_epoch (already known at every call site) instead of a
// separate "current date" config knob keeps the served weather always in
// sync with whichever weights.bin is active — there is no way for them to
// drift apart.
std::shared_ptr<maritime::WeatherBuffer>
load_weather_buffer(const std::string& avg_weather_dir, int64_t base_epoch)
{
    const auto day = std::chrono::floor<std::chrono::days>(
        std::chrono::system_clock::from_time_t(static_cast<time_t>(base_epoch)));
    const std::chrono::year_month_day ymd{day};

    auto buf = maritime::weather_etl::AvgWeatherLoader::load(
        avg_weather_dir,
        static_cast<int>(ymd.year()),
        static_cast<unsigned>(ymd.month()),
        static_cast<unsigned>(ymd.day()));
    buf->base_epoch = base_epoch;
    return buf;
}

} // namespace

QueryServer::QueryServer(const ServerConfig& cfg)
    : engine_(cfg.graph_path, cfg.flags_path, cfg.snap_wave_path, cfg.snap_wind_path, cfg.cch_topo_path)
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

        if (!cfg_.avg_weather_dir.empty()) {
            engine_.update_weather(load_weather_buffer(cfg_.avg_weather_dir, payload.base_epoch));
            std::cout << "[router_server] Initial WeatherBuffer loaded from "
                      << cfg_.avg_weather_dir << "\n";
        }
    } else {
        std::cout << "[router_server] No weights.bin found at startup — "
                     "waiting for ETL\n";
    }

    // poll_interval_s == 0 means one-shot: serve whatever was loaded above
    // and never re-check weights.bin. Skipping the thread avoids a
    // sleep_for(0s) busy-spin for callers that only need a single query
    // (e.g. the Python bindings) and don't intend to call stop() promptly.
    if (cfg_.poll_interval_s > 0)
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

            if (!cfg_.avg_weather_dir.empty()) {
                engine_.update_weather(
                    load_weather_buffer(cfg_.avg_weather_dir, payload.base_epoch));
                std::cout << "[router_server] WeatherBuffer updated from "
                          << cfg_.avg_weather_dir << "\n";
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
