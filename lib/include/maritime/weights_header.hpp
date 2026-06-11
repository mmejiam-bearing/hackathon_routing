#pragma once

#include <cstdint>

namespace maritime {

// On-disk header for weights.bin — the interface contract between weather_etl
// (writer) and router_server (reader).  Lives in lib/ so neither program
// needs to include the other's private headers.
//
// [WeightsHeader]
// [uint32  weight[n_edges]]   proxy FOC cost scaled by 1e3, minimum 1
struct WeightsHeader {
    uint32_t magic      = 0;  // 0x54484757 "WGHT" LE
    uint32_t version    = 0;  // currently 1
    uint32_t n_edges    = 0;
    uint32_t reserved   = 0;
    int64_t  base_epoch = 0;  // UNIX timestamp of the weather cycle start
};

} // namespace maritime
