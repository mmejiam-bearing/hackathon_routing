#pragma once

#include "maritime/routing_engine.hpp"
#include "weights_loader.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

namespace maritime::router_server {

struct ServerConfig {
    std::string graph_path;
    std::string flags_path;
    std::string snap_path;
    std::string cch_topo_path;
    std::string weights_dir;
    std::string npy_dir;        // directory containing {var}.npy (optional — empty = skip FOC)
    uint32_t    poll_interval_s = 30;
};

class QueryServer {
public:
    explicit QueryServer(const ServerConfig& cfg);
    ~QueryServer();

    void stop() noexcept;
    [[nodiscard]] maritime::CchRouteResult serve_query(const maritime::CchRouteRequest& req);

private:
    void poll_loop();

    maritime::RoutingEngine engine_;
    ServerConfig cfg_;
    std::atomic<bool> running_{true};
    std::thread poll_thread_;
    std::filesystem::file_time_type last_weights_mtime_{};
};

} // namespace maritime::router_server
