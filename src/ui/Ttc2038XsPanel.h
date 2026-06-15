#pragma once

#include "canopen/CanOpenClient.h"
#include "canopen/PcanBackend.h"
#include "canopen/devices/Ttc2038Xs.h"
#include "canopen/devices/Ttc2038XsDevice.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bmu_app
{

// UI sub-panel for the TTC 2038XS CANopen safety I/O module.
// Renders as a tab-item child — does not call Begin/End itself.
// Owns its own CanOpenClient (PCAN channel) and a device abstraction that
// hides the CANopen object-dictionary details behind typed Status / Control /
// Configure operations.
class Ttc2038XsPanel
{
  public:
    Ttc2038XsPanel() : device_(client_)
    {
    }

    void render();

  private:
    void render_connection();
    void render_header_bar();

    // Node-ID discovery (passive inference + active SDO scan).
    void render_discovery();
    // Device-oriented tabs.
    void render_status_tab();
    void render_control_tab();
    void render_configure_tab();
    void render_pins_tab();
    void render_advanced_tab();
    void render_traffic_tab();

    canopen::CanOpenClient client_;
    canopen::ttc2038xs::Ttc2038XsDevice device_;

    // Connection settings
    std::vector<canopen::PcanChannelInfo> channels_;
    int channel_idx_{0};
    int bitrate_idx_{1}; // default 500 kbit/s
    int mode_idx_{0};    // 0 = Monitor, 1 = Control
    int node_id_{10};    // remote TTC node-id (device default is 10)

    // Control staging
    bool do_bits_[8]{};
    bool safe_state_requested_{false};

    // Development mode: user must arm before the enable button is clickable
    // (it writes to safety-configuration objects and bypasses verification).
    bool dev_mode_arm_{false};

    // Control tab: per-pin output write staging (PWM/LPO duty cycle), keyed by
    // flat pin handle. Auto-refresh polls configured input/output values.
    std::vector<int> pin_io_edit_;
    bool control_auto_refresh_{false};

    // Configure staging: editable integer buffer per parameter handle.
    std::vector<int> config_edit_;

    // Pins staging: selected mode-list index per flat pin handle.
    std::vector<int> pin_mode_edit_;

    // Advanced raw object access
    int raw_index_{0x1000};
    int raw_sub_{0};
    char raw_write_hex_[64]{""};

    // Traffic view
    bool traffic_paused_{false};
    bool identity_requested_{false};

    // Node-ID discovery UI state
    bool show_discovery_{false};
    int scan_first_{1};
    int scan_last_{127};
};

} // namespace bmu_app
