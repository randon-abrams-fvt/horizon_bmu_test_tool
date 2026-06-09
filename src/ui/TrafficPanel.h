#pragma once

#include "runtime/BmuRuntime.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace bmu_app
{

// Shows a summary table of unique message types with count and cycle-time
// statistics. Renders as a child region — does not call Begin/End itself.
class TrafficPanel
{
  public:
    void push_event(const BmuTrafficEvent &event);

    void render();

    void clear();

  private:
    using Clock = std::chrono::steady_clock;

    struct MessageStats
    {
        std::string type_name;
        std::string origin;
        std::string s_id;
        std::string d_id;
        bool is_rx{true};
        uint64_t count{0};
        Clock::time_point first_seen{};
        Clock::time_point last_seen{};
        double min_cycle_ms{0.0};
        double max_cycle_ms{0.0};
        double avg_cycle_ms{0.0};
        double cycle_sum_ms{0.0};
        std::vector<uint8_t> last_payload;
    };

    // Key: "RX:TypeName" or "TX:TypeName"
    std::unordered_map<std::string, MessageStats> stats_;
    // Ordered keys for stable rendering
    std::vector<std::string> ordered_keys_;

    char filter_buf_[128]{};
};

} // namespace bmu_app
