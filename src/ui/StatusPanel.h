#pragma once

#include "runtime/BmuRuntime.h"

namespace bmu_app
{

// Displays the live BmuStatus fields received from the BMU.
// Renders as a tab-item child — does not call Begin/End itself.
class StatusPanel
{
  public:
    void set_runtime(BmuRuntime *runtime)
    {
        runtime_ = runtime;
    }

    // Called once per frame with the latest snapshot (already drained by App).
    void push_status(const BmuStatusSnapshot &snap)
    {
        snapshot_ = snap;
    }

    void render();

  private:
    void render_digital_output_grid();

    BmuRuntime *runtime_{nullptr};
    BmuStatusSnapshot snapshot_{};
};

} // namespace bmu_app
