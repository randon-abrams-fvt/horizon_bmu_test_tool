#pragma once

#include "runtime/BmuRuntime.h"
#include "ui/CommandPanel.h"
#include "ui/StatusPanel.h"
#include "ui/TrafficPanel.h"

#include <memory>
#include <string>

namespace bmu_app
{

class App
{
  public:
    App();
    ~App();

    // Called once per ImGui frame between NewFrame() and Render().
    void render();

  private:
    void render_menu_bar();
    void render_connect_window();
    void render_settings_window();

    void connect();
    void disconnect();

    // ── Runtime ──────────────────────────────────────────────────────────────
    BmuRuntime runtime_;

    // ── Panels ───────────────────────────────────────────────────────────────
    StatusPanel status_panel_;
    CommandPanel command_panel_;
    TrafficPanel traffic_panel_;

    // ── Connection dialog state
    // ───────────────────────────────────────────────
    bool show_connect_window_{false};
    bool show_settings_window_{false};
    char host_buf_[64]{"10.0.0.2"};
    int port_{19000};
    std::string connect_error_;

    // Paths to proto descriptor / yaml — resolved relative to the executable.
    std::string desc_path_;
    std::string yaml_path_;

    // ── Layout ───────────────────────────────────────────────────────────────
    float upper_ratio_{0.40f}; // fraction of vertical space for top panel

    // ── Settings ─────────────────────────────────────────────────────────────
    float text_scale_{1.0f};
    int theme_idx_{0}; // 0=Dark, 1=Light, 2=Classic
};

} // namespace bmu_app
