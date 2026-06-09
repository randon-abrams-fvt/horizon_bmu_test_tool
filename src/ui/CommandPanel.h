#pragma once

#include "runtime/BmuRuntime.h"

namespace bmu_app
{

// TX panel — lets the user build and send BmuCommand messages to the BMU.
// Renders as a tab-item child — does not call Begin/End itself.
class CommandPanel
{
  public:
    void set_runtime(BmuRuntime *runtime)
    {
        runtime_ = runtime;
    }

    void render();

  private:
    void render_connect_controls();
    void render_cyclic_controls();
    bool try_send_command();

    BmuRuntime *runtime_{nullptr};

    // Command field state
    uint32_t bmu_node_id_{1};
    bool hv_connect_{false};
    bool hvil_closed_{false};

    // Cyclic TX state
    bool cyclic_enabled_{false};
    float cyclic_hz_{1.0f};
    double cyclic_accum_s_{0.0};
    double last_frame_time_s_{0.0};

    // Feedback
    std::string last_send_status_;
};

} // namespace bmu_app
