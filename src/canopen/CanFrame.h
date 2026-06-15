#pragma once

#include <array>
#include <cstdint>

namespace bmu_app::canopen
{

// A generic CAN frame, independent of any particular adapter backend.
struct CanFrame
{
    uint32_t id{0};       // 11-bit (or 29-bit when extended) identifier
    bool extended{false}; // true for 29-bit identifiers
    bool rtr{false};      // remote transmission request
    uint8_t dlc{0};       // number of valid bytes in data (0..8)
    std::array<uint8_t, 8> data{}; // payload

    // Wall-clock receive timestamp in microseconds since steady epoch.
    // Zero for frames we build for transmission.
    uint64_t timestamp_us{0};
};

} // namespace bmu_app::canopen
