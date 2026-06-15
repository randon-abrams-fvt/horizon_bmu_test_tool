#include "Ttc2038XsPanel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace bmu_app
{

using namespace canopen;
namespace ttc = canopen::ttc2038xs;

namespace
{

constexpr std::array<PcanBitrate, 8> kBitrates = {
    PcanBitrate::Kbit_1000,
    PcanBitrate::Kbit_500,
    PcanBitrate::Kbit_250,
    PcanBitrate::Kbit_125,
    PcanBitrate::Kbit_100,
    PcanBitrate::Kbit_50,
    PcanBitrate::Kbit_20,
    PcanBitrate::Kbit_10,
};

const ImVec4 kGreen{0.30f, 0.85f, 0.40f, 1.0f};
const ImVec4 kRed{1.0f, 0.40f, 0.40f, 1.0f};
const ImVec4 kAmber{1.0f, 0.70f, 0.25f, 1.0f};
const ImVec4 kGrey{0.55f, 0.55f, 0.55f, 1.0f};

std::string bytes_to_hex(const std::vector<uint8_t> &bytes)
{
    std::string s;
    char tmp[4];
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        std::snprintf(tmp, sizeof(tmp), "%02X", bytes[i]);
        if (i > 0)
        {
            s += ' ';
        }
        s += tmp;
    }
    return s;
}

// Parses "01 FF 2A" or "01FF2A" into bytes. Returns false on a malformed input.
bool parse_hex(const char *text, std::vector<uint8_t> &out)
{
    out.clear();
    std::string nibbles;
    for (const char *p = text; *p != '\0'; ++p)
    {
        if (*p == ' ' || *p == ',' || *p == '\t')
        {
            continue;
        }
        if (std::isxdigit(static_cast<unsigned char>(*p)) == 0)
        {
            return false;
        }
        nibbles += *p;
    }
    if (nibbles.empty() || (nibbles.size() % 2) != 0)
    {
        return false;
    }
    for (size_t i = 0; i < nibbles.size(); i += 2)
    {
        const std::string byte_str = nibbles.substr(i, 2);
        out.push_back(
            static_cast<uint8_t>(std::strtoul(byte_str.c_str(), nullptr, 16)));
    }
    return true;
}

ImVec4 nmt_color(NmtState s)
{
    switch (s)
    {
    case NmtState::Operational:
        return kGreen;
    case NmtState::PreOperational:
        return kAmber;
    case NmtState::Stopped:
        return kRed;
    case NmtState::BootUp:
        return kAmber;
    default:
        return kGrey;
    }
}

} // namespace

void Ttc2038XsPanel::render()
{
    render_connection();

    if (!client_.is_connected())
    {
        return;
    }

    // Service the device's SDO sequencer once per frame.
    device_.poll();

    // Read identity once after connecting in Control mode.
    if (!identity_requested_ && device_.is_control())
    {
        device_.refresh_identity();
        identity_requested_ = true;
    }

    ImGui::Spacing();
    render_header_bar();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##ttc_tabs"))
    {
        if (ImGui::BeginTabItem("Status"))
        {
            render_status_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Control"))
        {
            render_control_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Configure"))
        {
            render_configure_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Pins"))
        {
            render_pins_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Advanced"))
        {
            render_advanced_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("CAN Traffic"))
        {
            render_traffic_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ── Connection
// ────────────────────────────────────────────────────────────────
void Ttc2038XsPanel::render_connection()
{
    ImGui::SeparatorText("Connection");

    if (!PcanChannel::driver_available())
    {
        ImGui::TextColored(
            kAmber,
            "PCANBasic.dll not found. Install the PEAK driver to use CAN.");
    }

    const bool connected = client_.is_connected();
    ImGui::BeginDisabled(connected);

    if (ImGui::Button("Scan"))
    {
        channels_ = PcanChannel::list_channels();
        channel_idx_ = 0;
    }
    ImGui::SameLine();
    {
        std::string preview = channels_.empty() ? "PCAN-USB 1 (default)"
                                                : channels_[channel_idx_].name;
        ImGui::SetNextItemWidth(250.0f);
        if (ImGui::BeginCombo("Channel", preview.c_str()))
        {
            for (int i = 0; i < static_cast<int>(channels_.size()); ++i)
            {
                const bool sel = (i == channel_idx_);
                if (ImGui::Selectable(channels_[i].name.c_str(), sel))
                {
                    channel_idx_ = i;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SetNextItemWidth(250.0f);
    const char *bitrate_names[] = {
        "1 Mbit/s",
        "500 kbit/s",
        "250 kbit/s",
        "125 kbit/s",
        "100 kbit/s",
        "50 kbit/s",
        "20 kbit/s",
        "10 kbit/s"};
    ImGui::Combo("Bitrate", &bitrate_idx_, bitrate_names, 8);

    ImGui::SetNextItemWidth(250.0f);
    const char *mode_names[] = {"Control (master)", "Monitor (passive)"};
    ImGui::Combo("Mode", &mode_idx_, mode_names, 2);
    if (mode_idx_ == 1)
    {
        ImGui::SameLine();
        ImGui::TextColored(
            kAmber, "Stop the BMU app before controlling the bus.");
    }

    ImGui::SetNextItemWidth(250.0f);
    ImGui::InputInt("Device node-ID", &node_id_);
    if (node_id_ < 1)
        node_id_ = 1;
    if (node_id_ > 127)
        node_id_ = 127;

    ImGui::EndDisabled();

    ImGui::Spacing();
    if (!connected)
    {
        if (ImGui::Button("Connect", ImVec2(120.0f, 0.0f)))
        {
            const uint16_t handle =
                channels_.empty() ? static_cast<uint16_t>(0x51) // PCAN_USBBUS1
                                  : channels_[channel_idx_].handle;
            const ClientMode mode =
                (mode_idx_ == 0) ? ClientMode::Control : ClientMode::Monitor;
            if (client_.connect(handle, kBitrates[bitrate_idx_], mode))
            {
                device_.set_node_id(static_cast<uint8_t>(node_id_));
                device_.reset();
                identity_requested_ = false;
            }
            else
            {
                ImGui::OpenPopup("Connect failed");
            }
        }
    }
    else
    {
        ImGui::Text("Connected — %s", to_string(client_.mode()));
        ImGui::SameLine();
        if (ImGui::Button("Disconnect", ImVec2(120.0f, 0.0f)))
        {
            client_.disconnect();
            device_.reset();
        }
    }

    if (ImGui::BeginPopupModal(
            "Connect failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored(kRed, "%s", client_.last_error().c_str());
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (connected)
    {
        render_discovery();
    }
}

// ── Node-ID discovery
// ──────────────────────────────────────────────────────────
void Ttc2038XsPanel::render_discovery()
{
    if (!ImGui::CollapsingHeader("Find device node-ID"))
    {
        return;
    }

    ImGui::TextWrapped(
        "Passive: infers node IDs from observed traffic (works while "
        "monitoring the live BMU\xE2\x86\x94"
        "device link). Active: SDO-probes "
        "each node ID (Control mode only).");

    // Passive detection is always available; it just reads what was observed.
    ImGui::Spacing();

    const bool control = device_.is_control();
    const bool scanning = device_.scanning();

    ImGui::BeginDisabled(!control || scanning);
    if (ImGui::Button("Active scan"))
    {
        device_.start_node_scan(
            static_cast<uint8_t>(scan_first_),
            static_cast<uint8_t>(scan_last_));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("from", &scan_first_, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("to", &scan_last_, 0, 0);
    scan_first_ = scan_first_ < 1 ? 1 : (scan_first_ > 127 ? 127 : scan_first_);
    scan_last_ = scan_last_ < 1 ? 1 : (scan_last_ > 127 ? 127 : scan_last_);
    if (!control)
    {
        ImGui::SameLine();
        ImGui::TextColored(kAmber, "(Control mode only)");
    }

    if (scanning)
    {
        const int total = device_.scan_total();
        const float frac =
            total > 0 ? static_cast<float>(device_.scan_progress()) / total
                      : 0.0f;
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f));
        if (ImGui::Button("Cancel scan"))
        {
            device_.cancel_node_scan();
        }
    }

    // Results table (passive + probe-confirmed).
    const auto nodes = device_.detect_nodes();
    ImGui::Spacing();
    if (nodes.empty())
    {
        ImGui::TextDisabled(
            control ? "No nodes detected yet. Run an active scan or wait for "
                      "traffic."
                    : "No nodes detected yet. Waiting for bus traffic...");
        return;
    }

    if (ImGui::BeginTable(
            "##detected",
            4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Node");
        ImGui::TableSetupColumn("Evidence");
        ImGui::TableSetupColumn("NMT");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (const auto &n : nodes)
        {
            ImGui::TableNextRow();
            ImGui::PushID(n.node_id);

            ImGui::TableNextColumn();
            ImGui::Text("%u (0x%02X)", n.node_id, n.node_id);

            ImGui::TableNextColumn();
            std::string ev;
            if (n.probed)
                ev += "probe ";
            if (n.heartbeat)
                ev += "HB ";
            if (n.sdo)
                ev += "SDO ";
            if (n.tpdo)
                ev += "TPDO ";
            if (n.emcy)
                ev += "EMCY ";
            if (ev.empty())
                ev = "-";
            ImGui::TextUnformatted(ev.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(to_string(n.nmt));

            ImGui::TableNextColumn();
            const bool current = (n.node_id == device_.node_id());
            if (current)
            {
                ImGui::TextColored(kGreen, "selected");
            }
            else if (ImGui::SmallButton("Use"))
            {
                node_id_ = n.node_id;
                device_.set_node_id(n.node_id);
                identity_requested_ = false;
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ── Header / summary bar
// ────────────────────────────────────────────────────────
void Ttc2038XsPanel::render_header_bar()
{
    const auto &st = device_.status();

    // Device identity.
    if (st.identity_valid && !st.device_name.empty())
    {
        ImGui::Text("%s", st.device_name.c_str());
    }
    else
    {
        ImGui::TextUnformatted("TTC 2038XS");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("node %u", device_.node_id());

    // NMT state.
    ImGui::SameLine();
    ImGui::TextUnformatted("|  NMT:");
    ImGui::SameLine();
    ImGui::TextColored(nmt_color(st.nmt), "%s", to_string(st.nmt));
    // Flag a stale NMT state (no recent 0x700 frame) so a latched "Boot-up" is
    // not mistaken for the live state.
    {
        const double hb_age = device_.seconds_since_heartbeat();
        if (hb_age < 0.0 || hb_age > 3.0)
        {
            ImGui::SameLine();
            ImGui::TextColored(kAmber, "(stale)");
        }
    }

    // Error register summary.
    ImGui::SameLine();
    ImGui::TextUnformatted("|  Errors:");
    ImGui::SameLine();
    if (!st.error_register_valid)
    {
        ImGui::TextColored(kGrey, "?");
    }
    else if (st.error_register == 0)
    {
        ImGui::TextColored(kGreen, "none");
    }
    else
    {
        ImGui::TextColored(kRed, "0x%02X", st.error_register);
    }

    if (device_.busy())
    {
        ImGui::SameLine();
        ImGui::TextColored(kAmber, "|  reading...");
    }

    // Status polling controls.
    if (!device_.is_control())
    {
        ImGui::TextColored(
            kAmber, "Monitor mode: status/configuration require Control mode.");
        return;
    }

    bool ar = device_.auto_refresh();
    if (ImGui::Checkbox("Auto-refresh status (1 Hz)", &ar))
    {
        device_.set_auto_refresh(ar, 1.0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh now"))
    {
        device_.refresh_status();
    }
    ImGui::SameLine();
    if (ImGui::Button("Read identity"))
    {
        device_.refresh_identity();
    }
}

// ── Status tab
// ──────────────────────────────────────────────────────────────────
void Ttc2038XsPanel::render_status_tab()
{
    const auto &st = device_.status();

    if (ImGui::BeginChild("##status", ImVec2(0.0f, 0.0f)))
    {
        // ECU diagnostic state (0x2400) — the key safety state indicator.
        ImGui::SeparatorText("Device state");
        if (st.ecu_state_valid)
        {
            ImVec4 col = kGrey;
            switch (st.ecu_state)
            {
            case 1:
                col = kGreen;
                break; // running
            case 2:
                col = kRed;
                break; // safe state
            default:
                col = kAmber;
                break; // startup / other
            }
            ImGui::TextColored(
                col,
                "ECU state: %s (0x%02X)",
                ttc::ecu_state_text(st.ecu_state),
                st.ecu_state);
            if (st.ecu_state != 1)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(outputs applied only when 'running')");
            }
        }
        else
        {
            ImGui::TextDisabled("ECU state: —");
        }

        // Non-recoverable safe-state indicator (any SMU alarm set).
        if (st.smu_alarms_valid)
        {
            bool smu_alarm = false;
            for (auto a : st.smu_alarms)
            {
                if (a != 0)
                {
                    smu_alarm = true;
                    break;
                }
            }
            if (smu_alarm)
            {
                ImGui::TextColored(
                    kRed,
                    "SMU alarm active — safe state is NON-recoverable "
                    "(power cycle required)");
            }
            else
            {
                ImGui::TextColored(
                    kGreen, "No SMU alarm (safe state recoverable)");
            }
        }

        // Supply voltages.
        if (st.supply_cpu_valid || st.supply_power_valid)
        {
            if (st.supply_cpu_valid)
            {
                ImGui::Text("BAT+ CPU: %.2f V", st.supply_cpu_mv / 1000.0);
            }
            if (st.supply_power_valid)
            {
                if (st.supply_cpu_valid)
                {
                    ImGui::SameLine();
                }
                ImGui::Text(
                    "   BAT+ Power: %.2f V", st.supply_power_mv / 1000.0);
            }
        }

        // Diagnostics row: temperature + error bits.
        ImGui::SeparatorText("Diagnostics");
        if (st.board_temp_valid)
        {
            ImGui::Text(
                "Board temperature: %.1f \xC2\xB0"
                "C",
                st.board_temp_decideg / 10.0);
        }
        else
        {
            ImGui::TextDisabled("Board temperature: —");
        }

        if (st.error_register_valid)
        {
            const auto bits = ttc::decode_error_register(st.error_register);
            if (st.error_register == 0)
            {
                ImGui::TextColored(kGreen, "Error register: no active faults");
            }
            else
            {
                ImGui::TextColored(
                    kRed, "Error register: 0x%02X", st.error_register);
                for (const auto &b : bits)
                {
                    if (b.set)
                    {
                        ImGui::BulletText("%s", b.name);
                    }
                }
            }
        }
        else
        {
            ImGui::TextDisabled("Error register: —");
        }

        // Reporting device (0x2401 / 0x2402) for the active error.
        if (st.reporting_valid && st.reporting_device != 0)
        {
            ImGui::TextColored(
                kAmber,
                "Reporting device: %u  (status 0x%08X)",
                st.reporting_device,
                st.reporting_device_status);
        }

        // Predefined error field (0x1003): the actual EMCY codes behind the
        // generic error bit.
        if (st.predef_error_valid)
        {
            if (st.predef_error_count == 0)
            {
                ImGui::TextColored(kGreen, "Error history: empty");
            }
            else
            {
                ImGui::Text(
                    "Error history (%u, newest first):", st.predef_error_count);
                for (uint8_t i = 0; i < st.predef_error_count; ++i)
                {
                    const uint32_t entry = st.predef_errors[i];
                    const uint16_t code = static_cast<uint16_t>(entry & 0xFFFF);
                    const uint16_t info =
                        static_cast<uint16_t>((entry >> 16) & 0xFFFF);
                    ImGui::BulletText(
                        "0x%04X  %s  (info 0x%04X)",
                        code,
                        ttc::emcy_error_text(code),
                        info);
                }
            }
        }

        // Core component health (0x2403): a fault here is fatal and forces the
        // safe state. This is usually the real cause behind a 0x5000 hardware
        // error in the error history.
        ImGui::SeparatorText("Core component health");
        if (st.device_status_valid)
        {
            if (ImGui::BeginTable(
                    "##devstatus",
                    2,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Component");
                ImGui::TableSetupColumn(
                    "State", ImGuiTableColumnFlags_WidthFixed, 500.0f);
                ImGui::TableHeadersRow();
                for (int i = 0; i < ttc::DeviceStatus::kDeviceStatusCount; ++i)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(ttc::device_status_name(i));
                    ImGui::TableSetColumnIndex(1);
                    const uint32_t s = st.device_status[i];
                    if (s == 0)
                    {
                        ImGui::TextColored(kGreen, "OK");
                    }
                    else
                    {
                        ImGui::TextColored(
                            kRed,
                            "0x%08X — %s",
                            s,
                            ttc::error_status_text(
                                static_cast<uint16_t>(s & 0xFFFF)));
                    }
                }
                ImGui::EndTable();
            }
        }
        else
        {
            ImGui::TextDisabled("Not read yet — use Refresh now.");
        }

        // Safety switch.
        ImGui::SeparatorText("Safety switch 1");
        if (st.ssw_valid)
        {
            const uint16_t code = static_cast<uint16_t>(st.ssw_status & 0xFFFF);
            if (st.ssw_status == 0)
            {
                ImGui::TextColored(kGreen, "Status: OK (0x00000000)");
            }
            else
            {
                ImGui::TextColored(
                    kAmber,
                    "Status: 0x%08X — %s",
                    st.ssw_status,
                    ttc::error_status_text(code));
            }
            ImGui::Text("Enable feedback: %u", st.ssw_enable_feedback);
        }
        else
        {
            ImGui::TextDisabled("—");
        }

        // Digital inputs as a 32-bit indicator grid.
        ImGui::SeparatorText("Digital inputs (32)");
        if (st.digital_inputs_valid)
        {
            for (int i = 0; i < 32; ++i)
            {
                const bool on = (st.digital_inputs >> i) & 0x1u;
                if (i % 8 != 0)
                {
                    ImGui::SameLine();
                }
                ImGui::TextColored(
                    on ? kGreen : kGrey, "%2d:%s", i + 1, on ? "1" : "0");
            }
        }
        else
        {
            ImGui::TextDisabled("Not read yet — use Refresh now.");
        }

        // Analog inputs table.
        ImGui::SeparatorText("Analog inputs (32)");
        if (st.analog_inputs_valid)
        {
            if (ImGui::BeginTable(
                    "##ai",
                    4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchSame))
            {
                for (int row = 0; row < 8; ++row)
                {
                    ImGui::TableNextRow();
                    for (int col = 0; col < 4; ++col)
                    {
                        const int ch = row + col * 8;
                        ImGui::TableSetColumnIndex(col);
                        ImGui::Text("AI%2d: %6d", ch + 1, st.analog_inputs[ch]);
                    }
                }
                ImGui::EndTable();
            }
        }
        else
        {
            ImGui::TextDisabled("Not read yet — use Refresh now.");
        }
    }
    ImGui::EndChild();
}

// ── Control tab
// ─────────────────────────────────────────────────────────────────
void Ttc2038XsPanel::render_control_tab()
{
    const bool control = device_.is_control();
    const auto &st = device_.status();
    const auto &groups = ttc::pin_groups();
    const size_t pin_total = device_.pin_mode_count();
    if (pin_io_edit_.size() != pin_total)
    {
        pin_io_edit_.assign(pin_total, 0);
    }

    if (!control)
    {
        ImGui::TextColored(
            kAmber,
            "Connect in Control mode to drive the device. In Monitor mode the\n"
            "tool only observes the BMU<->device conversation.");
    }

    ImGui::BeginDisabled(!control);

    // ── NMT ──────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Network management (NMT)");
    if (ImGui::Button("Start (Operational)"))
    {
        device_.start_node();
    }
    ImGui::SameLine();
    if (ImGui::Button("Pre-Operational"))
    {
        device_.enter_pre_operational();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {
        device_.stop_node();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset node"))
    {
        device_.reset_node();
    }

    // Live NMT state + active polling. The device only sends 0x700 frames on
    // boot-up and (if enabled) as periodic heartbeats, so the latched state can
    // be stale. Show how fresh it is and offer an active probe.
    ImGui::Spacing();
    ImGui::TextUnformatted("Current NMT state:");
    ImGui::SameLine();
    ImGui::TextColored(nmt_color(st.nmt), "%s", to_string(st.nmt));
    ImGui::SameLine();
    const double hb_age = device_.seconds_since_heartbeat();
    const bool hb_stale = (hb_age < 0.0) || (hb_age > 3.0);
    if (hb_age < 0.0)
    {
        ImGui::TextColored(
            kAmber, "(no 0x700 frame seen — state unknown/stale)");
    }
    else if (hb_stale)
    {
        ImGui::TextColored(
            kAmber, "(stale: last 0x700 %.1fs ago)", hb_age);
    }
    else
    {
        ImGui::TextColored(kGrey, "(via heartbeat %.1fs ago)", hb_age);
    }

    if (ImGui::Button("Poll state now"))
    {
        device_.poll_nmt_state();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(SDO read of 0x1017 — proves the node is alive)");

    // Probe result line.
    if (st.nmt_probe_pending)
    {
        ImGui::TextColored(kAmber, "Probing...");
    }
    else if (st.nmt_probe_done)
    {
        if (st.nmt_probe_alive)
        {
            ImGui::TextColored(
                kGreen,
                "Node responded — alive and communicating (NMT byte: %s).",
                to_string(st.nmt));
        }
        else
        {
            ImGui::TextColored(
                kRed,
                "No response: %s. Check power/T15/wiring/bitrate/node-ID.",
                st.nmt_probe_error.c_str());
        }
    }

    // Heartbeat helper: enabling the producer heartbeat makes the device report
    // its NMT state continuously so the display stays live.
    if (ImGui::Button("Enable heartbeat (1s)"))
    {
        device_.set_producer_heartbeat(1000);
    }
    ImGui::SameLine();
    if (ImGui::Button("Disable heartbeat"))
    {
        device_.set_producer_heartbeat(0);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(writes 0x1017; default is 0 = off)");


    ImGui::SeparatorText("I/O (by configured pin mode)");

    // Count how many pins have a known mode so we can prompt the user.
    size_t modes_known = 0;
    for (size_t i = 0; i < pin_total; ++i)
    {
        if (device_.pin_mode_value(i).valid)
        {
            ++modes_known;
        }
    }

    if (ImGui::Button("Read pin modes"))
    {
        device_.read_all_pin_modes();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh I/O now"))
    {
        device_.read_all_pin_io();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Auto-refresh", &control_auto_refresh_))
    {
        // Reuse the device auto-refresh cadence for the status block; pin I/O is
        // refreshed from here each frame the toggle is on (throttled by busy()).
    }
    if (control_auto_refresh_ && control && !device_.busy())
    {
        device_.read_all_pin_io();
    }

    if (modes_known == 0)
    {
        ImGui::TextColored(
            kAmber,
            "Pin modes unknown — click \"Read pin modes\" (or use the Pins tab) "
            "so sections can be populated.");
    }

    // Build the set of functions that actually have configured pins, then draw
    // one section per function in a fixed, readable order.
    const ttc::IoFunction kOrder[] = {
        ttc::IoFunction::DigitalOutput,
        ttc::IoFunction::PwmOutput,
        ttc::IoFunction::LpoOutput,
        ttc::IoFunction::DigitalInput,
        ttc::IoFunction::AnalogInput,
        ttc::IoFunction::TimerInput,
        ttc::IoFunction::SentInput,
    };

    for (ttc::IoFunction func : kOrder)
    {
        // Collect configured pins for this function.
        struct Entry
        {
            size_t handle;
            const ttc::PinGroup *group;
            const ttc::PinInfo *pin;
            ttc::ModeBehavior beh;
            uint8_t mode;
        };
        std::vector<Entry> entries;
        for (size_t gi = 0; gi < groups.size(); ++gi)
        {
            const auto &group = groups[gi];
            for (size_t pi = 0; pi < group.pins.size(); ++pi)
            {
                const size_t handle =
                    ttc::Ttc2038XsDevice::pin_mode_handle(gi, pi);
                const auto &pm = device_.pin_mode_value(handle);
                if (!pm.valid)
                {
                    continue;
                }
                const ttc::ModeBehavior beh = ttc::mode_behavior(pm.mode);
                if (beh.function != func)
                {
                    continue;
                }
                entries.push_back(
                    {handle, &group, &group.pins[pi], beh, pm.mode});
            }
        }
        if (entries.empty())
        {
            continue; // no pins configured for this function — hide the section
        }

        ImGui::SeparatorText(ttc::to_string(func));

        if (func == ttc::IoFunction::DigitalOutput)
        {
            // Interactive tiles. Outputs are commanded via the CiA 401 aggregate
            // object 0x6200 (DO 1-8 bitfield); each configured DOP pin maps to
            // one DO bit by its position among the DOP group's pins. The device
            // read-back (0x6200) confirms the applied state.
            ImGui::TextDisabled(
                "Click a tile to toggle. Red dot = commanded on, green = "
                "confirmed on by device (aggregate object 0x6200).");

            const ImVec2 tile_size(76.0f, 56.0f);
            ImDrawList *draw = ImGui::GetWindowDrawList();
            for (size_t k = 0; k < entries.size(); ++k)
            {
                const auto &e = entries[k];
                // DO bit index = the pin's position within its group (DOP pins
                // are sub 1..N and map to DO bits 0..N-1).
                const int bit = static_cast<int>(e.pin->sub) - 1;
                if (bit < 0 || bit > 7)
                {
                    continue;
                }
                const bool commanded = do_bits_[bit];
                const bool actual_on =
                    st.digital_outputs_status_valid &&
                    ((st.digital_outputs_status >> bit) & 0x1u) != 0;

                if (k % 4 != 0)
                {
                    ImGui::SameLine();
                }

                ImVec4 base = actual_on ? ImVec4(0.16f, 0.55f, 0.22f, 1.0f)
                                        : ImVec4(0.20f, 0.21f, 0.24f, 1.0f);
                ImVec4 hov = actual_on ? ImVec4(0.20f, 0.66f, 0.28f, 1.0f)
                                       : ImVec4(0.28f, 0.29f, 0.33f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, base);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, hov);

                char label[24];
                std::snprintf(
                    label,
                    sizeof(label),
                    "%s\n%s",
                    e.pin->label,
                    actual_on ? "ON" : "off");
                ImGui::PushID(static_cast<int>(e.handle));
                if (ImGui::Button(label, tile_size))
                {
                    do_bits_[bit] = !do_bits_[bit];
                    uint8_t mask = 0;
                    for (int b = 0; b < 8; ++b)
                    {
                        if (do_bits_[b])
                        {
                            mask |= static_cast<uint8_t>(1u << b);
                        }
                    }
                    device_.set_digital_outputs(mask);
                }
                ImGui::PopID();
                ImGui::PopStyleColor(3);

                if (commanded)
                {
                    const ImVec2 rmin = ImGui::GetItemRectMin();
                    const ImVec2 rmax = ImGui::GetItemRectMax();
                    const ImVec2 center(rmax.x - 9.0f, rmin.y + 9.0f);
                    draw->AddCircleFilled(
                        center, 5.0f, IM_COL32(230, 40, 40, 255));
                    draw->AddCircle(
                        center, 5.0f, IM_COL32(120, 0, 0, 255), 0, 1.5f);
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Refresh DO status"))
            {
                device_.read_digital_output_status();
            }
        }
        else if (
            func == ttc::IoFunction::PwmOutput ||
            func == ttc::IoFunction::LpoOutput)
        {
            // Editable duty-cycle rows.
            char tbl[32];
            std::snprintf(tbl, sizeof(tbl), "##out_%d", static_cast<int>(func));
            if (ImGui::BeginTable(
                    tbl,
                    4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Pin", 0, 0.20f);
                ImGui::TableSetupColumn("Mode", 0, 0.25f);
                ImGui::TableSetupColumn("Value", 0, 0.25f);
                ImGui::TableSetupColumn("Set", 0, 0.30f);
                ImGui::TableHeadersRow();
                for (const auto &e : entries)
                {
                    ImGui::PushID(static_cast<int>(e.handle));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", e.pin->label);
                    ImGui::SameLine();
                    ImGui::TextDisabled("sub%u", e.pin->sub);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        ttc::pin_mode_name(*e.group, e.mode));

                    ImGui::TableSetColumnIndex(2);
                    const auto &iv = device_.pin_io_value(e.handle);
                    if (iv.reading)
                    {
                        ImGui::TextColored(kAmber, "...");
                    }
                    else if (!iv.error.empty())
                    {
                        ImGui::TextColored(kRed, "%s", iv.error.c_str());
                    }
                    else if (!iv.valid)
                    {
                        ImGui::TextDisabled("—");
                    }
                    else
                    {
                        ImGui::Text(
                            "%lld %s",
                            static_cast<long long>(iv.value),
                            e.beh.unit);
                    }

                    ImGui::TableSetColumnIndex(3);
                    ImGui::SetNextItemWidth(90.0f);
                    ImGui::InputInt("##duty", &pin_io_edit_[e.handle], 0, 0);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!control);
                    if (ImGui::SmallButton("Write"))
                    {
                        device_.write_pin_io(
                            e.handle, pin_io_edit_[e.handle]);
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        else
        {
            // Read-only inputs (DIN / ADC / TIN / SENT).
            char tbl[32];
            std::snprintf(tbl, sizeof(tbl), "##in_%d", static_cast<int>(func));
            if (ImGui::BeginTable(
                    tbl,
                    3,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Pin", 0, 0.25f);
                ImGui::TableSetupColumn("Mode", 0, 0.35f);
                ImGui::TableSetupColumn("Value", 0, 0.40f);
                ImGui::TableHeadersRow();
                for (const auto &e : entries)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", e.pin->label);
                    ImGui::SameLine();
                    ImGui::TextDisabled("sub%u", e.pin->sub);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        ttc::pin_mode_name(*e.group, e.mode));

                    ImGui::TableSetColumnIndex(2);
                    const auto &iv = device_.pin_io_value(e.handle);
                    if (iv.reading)
                    {
                        ImGui::TextColored(kAmber, "...");
                    }
                    else if (!iv.error.empty())
                    {
                        ImGui::TextColored(kRed, "%s", iv.error.c_str());
                    }
                    else if (!iv.valid)
                    {
                        ImGui::TextDisabled("—");
                    }
                    else if (e.beh.function == ttc::IoFunction::DigitalInput)
                    {
                        const bool high = iv.value != 0;
                        ImGui::TextColored(
                            high ? kGreen : kGrey, "%s", high ? "HIGH" : "low");
                    }
                    else
                    {
                        ImGui::Text(
                            "%lld %s",
                            static_cast<long long>(iv.value),
                            e.beh.unit);
                    }
                }
                ImGui::EndTable();
            }
        }
    }

    // ── Safe state / diagnostics / persistence ────────────────────────────
    ImGui::SeparatorText("Safe state");
    if (ImGui::Checkbox("Request safe state", &safe_state_requested_))
    {
        device_.request_safe_state(safe_state_requested_);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(writes object 0x2008)");

    ImGui::SeparatorText("Diagnostics");
    if (ImGui::Button("Clear error history"))
    {
        device_.clear_error_history();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(resets predefined error field 0x1003)");

    ImGui::SeparatorText("Development mode");
    ImGui::TextWrapped(
        "Skips SRDO and pin-configuration verification when going Operational "
        "(useful to bring up a device that rejects its config, e.g. an OD-config "
        "fault). Bench use only.");
    ImGui::TextColored(
        kAmber,
        "Bypasses safety verification. The device emits a warning EMCY "
        "(0x0009 / 0xFE / 0xF00A) while active. Set NMT Pre-Operational first.");

    const bool pre_op = st.nmt == NmtState::PreOperational;
    if (!pre_op)
    {
        ImGui::TextColored(
            kAmber, "Device is not in Pre-Operational — writes may be rejected.");
    }

    ImGui::Checkbox("I understand (arm)", &dev_mode_arm_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!dev_mode_arm_);
    if (ImGui::Button("Enable dev mode"))
    {
        device_.enable_development_mode();
        dev_mode_arm_ = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Disable dev mode"))
    {
        device_.disable_development_mode();
        dev_mode_arm_ = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(0x2011 / 0x2010)");

    ImGui::SeparatorText("Persistence");
    if (ImGui::Button("Store to NVM"))
    {
        device_.store_parameters();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore defaults"))
    {
        device_.restore_defaults();
    }

    ImGui::EndDisabled();
}

// ── Configure tab
// ───────────────────────────────────────────────────────────────
void Ttc2038XsPanel::render_configure_tab()
{
    const auto &params = device_.parameters();
    if (config_edit_.size() != params.size())
    {
        config_edit_.assign(params.size(), 0);
    }

    const bool control = device_.is_control();
    ImGui::BeginDisabled(!control);
    if (ImGui::Button("Read all"))
    {
        device_.read_all_parameters();
    }
    ImGui::EndDisabled();
    if (!control)
    {
        ImGui::SameLine();
        ImGui::TextColored(kAmber, "Control mode required to read/write.");
    }

    // Render parameters grouped.
    const ttc::ParamGroup groups[] = {
        ttc::ParamGroup::Identity,
        ttc::ParamGroup::Communication,
        ttc::ParamGroup::DigitalIo,
        ttc::ParamGroup::AnalogIo,
        ttc::ParamGroup::Safety,
    };

    for (ttc::ParamGroup group : groups)
    {
        if (!ImGui::CollapsingHeader(
                ttc::to_string(group), ImGuiTreeNodeFlags_DefaultOpen))
        {
            continue;
        }

        char table_id[32];
        std::snprintf(
            table_id, sizeof(table_id), "##cfg_%d", static_cast<int>(group));
        if (!ImGui::BeginTable(
                table_id,
                4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp))
        {
            continue;
        }
        ImGui::TableSetupColumn(
            "Parameter", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn(
            "Value", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableSetupColumn(
            "Set", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.10f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto &def = params[i];
            if (def.group != group)
            {
                continue;
            }
            const auto &pv = device_.parameter_value(i);

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            // Name (+ unit).
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(def.name);
            if (def.unit && def.unit[0] != '\0')
            {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", def.unit);
            }

            // Current value.
            ImGui::TableSetColumnIndex(1);
            if (pv.reading)
            {
                ImGui::TextColored(kAmber, "...");
            }
            else if (!pv.error.empty())
            {
                ImGui::TextColored(kRed, "%s", pv.error.c_str());
            }
            else if (!pv.valid)
            {
                ImGui::TextDisabled("—");
            }
            else if (def.type == ttc::ParamType::String)
            {
                ImGui::TextUnformatted(pv.text.c_str());
            }
            else
            {
                ImGui::Text("%lld", static_cast<long long>(pv.integer));
            }

            // Editable value (RW numeric only).
            ImGui::TableSetColumnIndex(2);
            const bool writable = (def.access == ttc::ParamAccess::ReadWrite) &&
                                  (def.type != ttc::ParamType::String);
            if (writable)
            {
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputInt("##val", &config_edit_[i], 0, 0);
            }
            else
            {
                ImGui::TextDisabled(
                    def.access == ttc::ParamAccess::ReadOnly ? "read-only"
                                                             : "string");
            }

            // Read / Write buttons.
            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("Read"))
            {
                device_.read_parameter(i);
            }
            if (writable)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Write"))
                {
                    device_.write_parameter(i, config_edit_[i]);
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ── Pins tab
// ──────────────────────────────────────────────────────────────────
void Ttc2038XsPanel::render_pins_tab()
{
    const auto &groups = ttc::pin_groups();
    const size_t pin_total = device_.pin_mode_count();
    if (pin_mode_edit_.size() != pin_total)
    {
        pin_mode_edit_.assign(pin_total, 0);
    }

    ImGui::TextWrapped(
        "Per-pin I/O mode (object 0x3000 + group). Pin Mode is a configuration "
        "object: the device only accepts changes in the Pre-Operational NMT "
        "state. Use Control to read/write.");

    const bool control = device_.is_control();
    ImGui::BeginDisabled(!control);
    if (ImGui::Button("Read all pin modes"))
    {
        device_.read_all_pin_modes();
    }
    ImGui::EndDisabled();
    if (!control)
    {
        ImGui::SameLine();
        ImGui::TextColored(kAmber, "Control mode required to read/write.");
    }
    else if (device_.status().nmt != NmtState::PreOperational)
    {
        ImGui::SameLine();
        ImGui::TextColored(
            kAmber, "Writes require NMT Pre-Operational (see Control tab).");
    }

    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        const auto &group = groups[gi];

        char header[64];
        std::snprintf(
            header,
            sizeof(header),
            "%s  (0x%04X)###pgrp%zu",
            group.name,
            group.pin_mode_index,
            gi);
        if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
        {
            continue;
        }

        char table_id[32];
        std::snprintf(table_id, sizeof(table_id), "##pins_%zu", gi);
        if (!ImGui::BeginTable(
                table_id,
                4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp))
        {
            continue;
        }
        ImGui::TableSetupColumn(
            "Pin", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn(
            "Current mode", ImGuiTableColumnFlags_WidthStretch, 0.32f);
        ImGui::TableSetupColumn(
            "Set mode", ImGuiTableColumnFlags_WidthStretch, 0.32f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableHeadersRow();

        for (size_t pi = 0; pi < group.pins.size(); ++pi)
        {
            const auto &pin = group.pins[pi];
            const size_t handle =
                ttc::Ttc2038XsDevice::pin_mode_handle(gi, pi);
            const auto &pm = device_.pin_mode_value(handle);

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(handle));

            // Pin label (+ sub-index).
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(pin.label);
            ImGui::SameLine();
            ImGui::TextDisabled("sub%u", pin.sub);

            // Current mode (read back from the device).
            ImGui::TableSetColumnIndex(1);
            if (pm.reading)
            {
                ImGui::TextColored(kAmber, "...");
            }
            else if (!pm.error.empty())
            {
                ImGui::TextColored(kRed, "%s", pm.error.c_str());
            }
            else if (!pm.valid)
            {
                ImGui::TextDisabled("—");
            }
            else
            {
                ImGui::Text(
                    "%u (%s)",
                    pm.mode,
                    ttc::pin_mode_name(group, pm.mode));
            }

            // Mode selector (dropdown over the group's allowed modes).
            ImGui::TableSetColumnIndex(2);
            int &sel = pin_mode_edit_[handle];
            if (sel < 0 || sel >= static_cast<int>(group.modes.size()))
            {
                sel = 0;
            }
            const char *preview = group.modes[sel].name;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##mode", preview))
            {
                for (int m = 0; m < static_cast<int>(group.modes.size()); ++m)
                {
                    const bool selected = (sel == m);
                    char item[48];
                    std::snprintf(
                        item,
                        sizeof(item),
                        "%u .. %s",
                        group.modes[m].value,
                        group.modes[m].name);
                    if (ImGui::Selectable(item, selected))
                    {
                        sel = m;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            // Read / Write buttons.
            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("Read"))
            {
                device_.read_pin_mode(handle);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!control);
            if (ImGui::SmallButton("Write"))
            {
                device_.write_pin_mode(handle, group.modes[sel].value);
            }
            ImGui::EndDisabled();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Validate & apply configuration");
    ImGui::TextWrapped(
        "After setting pin modes above, apply them so the device will accept the "
        "configuration and allow Operational. This runs the documented sequence: "
        "write 0xA5 to Configuration Valid (0x13FE), read the computed signature "
        "from 0x2000, then write it to Safety Pin Config Signature (0x2010). It "
        "does NOT change the NMT state — send Start manually afterwards.");

    using ApplyState = ttc::Ttc2038XsDevice::ApplyConfigState;
    const ApplyState astate = device_.apply_config_state();
    const bool applying = astate == ApplyState::WritingConfigValid ||
                          astate == ApplyState::ReadingSignature ||
                          astate == ApplyState::WritingSignature;

    const bool pre_op2 = device_.status().nmt == NmtState::PreOperational;
    if (control && !pre_op2)
    {
        ImGui::TextColored(
            kAmber, "Device is not in Pre-Operational — apply may be rejected.");
    }

    ImGui::BeginDisabled(!control || applying);
    if (ImGui::Button("Validate & apply configuration"))
    {
        device_.apply_configuration();
    }
    ImGui::EndDisabled();

    // Status line.
    switch (astate)
    {
    case ApplyState::Idle:
        ImGui::SameLine();
        ImGui::TextDisabled("(not applied yet)");
        break;
    case ApplyState::WritingConfigValid:
    case ApplyState::ReadingSignature:
    case ApplyState::WritingSignature:
        ImGui::SameLine();
        ImGui::TextColored(kAmber, "%s", device_.apply_config_message().c_str());
        break;
    case ApplyState::Done:
        ImGui::SameLine();
        ImGui::TextColored(kGreen, "%s", device_.apply_config_message().c_str());
        break;
    case ApplyState::Failed:
        ImGui::SameLine();
        ImGui::TextColored(kRed, "%s", device_.apply_config_message().c_str());
        break;
    }

    if (astate == ApplyState::Done)
    {
        ImGui::TextColored(
            kGreen,
            "Now go to the Control tab and click \"Start (Operational)\".");
    }
    else if (astate == ApplyState::Failed)
    {
        ImGui::TextWrapped(
            "If the signature check fails the device sends an EMCY (0x0009 / "
            "0xFE / 0xF008). For bench bring-up you can instead use Development "
            "mode on the Control tab to skip verification.");
    }

    ImGui::Spacing();
    ImGui::TextDisabled(
        "Tip: after changing pin modes, persist with \"Store to NVM\" on the "
        "Control tab; a power cycle or reset applies a stored configuration.");
}

// ── Advanced (raw SDO) tab
// ──────────────────────────────────────────────────────
void Ttc2038XsPanel::render_advanced_tab()
{
    ImGui::TextWrapped(
        "Raw SDO access to any object-dictionary entry. Prefer the Configure "
        "tab for known parameters; this is an escape hatch for the full OD.");

    const bool control = device_.is_control();
    ImGui::BeginDisabled(!control);

    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt(
        "Index (hex)", &raw_index_, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
    if (raw_index_ < 0)
        raw_index_ = 0;
    if (raw_index_ > 0xFFFF)
        raw_index_ = 0xFFFF;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Sub", &raw_sub_, 0, 0);
    if (raw_sub_ < 0)
        raw_sub_ = 0;
    if (raw_sub_ > 0xFF)
        raw_sub_ = 0xFF;

    if (ImGui::Button("Read"))
    {
        device_.read_raw(
            static_cast<uint16_t>(raw_index_), static_cast<uint8_t>(raw_sub_));
    }

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText(
        "Write bytes (hex)", raw_write_hex_, sizeof(raw_write_hex_));
    ImGui::SameLine();
    if (ImGui::Button("Write"))
    {
        std::vector<uint8_t> bytes;
        if (parse_hex(raw_write_hex_, bytes) && !bytes.empty())
        {
            device_.write_raw(
                static_cast<uint16_t>(raw_index_),
                static_cast<uint8_t>(raw_sub_),
                bytes);
        }
    }

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextUnformatted("Result:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", device_.raw_result().c_str());
}

// ── Traffic tab
// ─────────────────────────────────────────────────────────────────
void Ttc2038XsPanel::render_traffic_tab()
{
    if (ImGui::Button("Clear"))
    {
        client_.clear_traffic();
    }
    ImGui::SameLine();
    if (ImGui::Button(traffic_paused_ ? "Resume live" : "Pause live"))
    {
        traffic_paused_ = !traffic_paused_;
    }

    if (ImGui::BeginChild("##traffic", ImVec2(0.0f, 0.0f), true))
    {
        if (ImGui::BeginTabBar("##traffic_tabs"))
        {
            if (ImGui::BeginTabItem("Statistics"))
            {
                auto stats = client_.cob_stats();
                std::sort(
                    stats.begin(),
                    stats.end(),
                    [](const CobStats &a, const CobStats &b) {
                        return a.cob_id < b.cob_id;
                    });
                if (ImGui::BeginTable(
                        "##statstbl",
                        8,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit))
                {
                    ImGui::TableSetupColumn("Dir");
                    ImGui::TableSetupColumn("COB-ID");
                    ImGui::TableSetupColumn("Count");
                    ImGui::TableSetupColumn("Cycle (ms)");
                    ImGui::TableSetupColumn("Last (ms)");
                    ImGui::TableSetupColumn("Min (ms)");
                    ImGui::TableSetupColumn("Max (ms)");
                    ImGui::TableSetupColumn("Last data");
                    ImGui::TableHeadersRow();
                    for (const auto &s : stats)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(s.tx ? "TX" : "RX");
                        ImGui::TableNextColumn();
                        ImGui::Text("0x%03X", s.cob_id);
                        ImGui::TableNextColumn();
                        ImGui::Text(
                            "%llu", static_cast<unsigned long long>(s.count));
                        ImGui::TableNextColumn();
                        if (s.count >= 2)
                            ImGui::Text("%.1f", s.avg_period_ms);
                        else
                            ImGui::TextUnformatted("-");
                        ImGui::TableNextColumn();
                        if (s.count >= 2)
                            ImGui::Text("%.1f", s.last_period_ms);
                        else
                            ImGui::TextUnformatted("-");
                        ImGui::TableNextColumn();
                        if (s.count >= 2)
                            ImGui::Text("%.1f", s.min_period_ms);
                        else
                            ImGui::TextUnformatted("-");
                        ImGui::TableNextColumn();
                        if (s.count >= 2)
                            ImGui::Text("%.1f", s.max_period_ms);
                        else
                            ImGui::TextUnformatted("-");
                        ImGui::TableNextColumn();
                        {
                            std::vector<uint8_t> d(
                                s.last_data.begin(),
                                s.last_data.begin() + s.last_dlc);
                            ImGui::TextUnformatted(bytes_to_hex(d).c_str());
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Live frames"))
            {
                if (!traffic_paused_)
                {
                    const auto records = client_.recent_traffic(300);
                    if (ImGui::BeginTable(
                            "##traffictbl",
                            4,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_ScrollY))
                    {
                        ImGui::TableSetupColumn("Dir");
                        ImGui::TableSetupColumn("COB-ID");
                        ImGui::TableSetupColumn("DLC");
                        ImGui::TableSetupColumn("Data");
                        ImGui::TableHeadersRow();
                        for (const auto &rec : records)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(rec.tx ? "TX" : "RX");
                            ImGui::TableNextColumn();
                            ImGui::Text("0x%03X", rec.frame.id);
                            ImGui::TableNextColumn();
                            ImGui::Text("%u", rec.frame.dlc);
                            ImGui::TableNextColumn();
                            std::vector<uint8_t> d(
                                rec.frame.data.begin(),
                                rec.frame.data.begin() + rec.frame.dlc);
                            ImGui::TextUnformatted(bytes_to_hex(d).c_str());
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
}

} // namespace bmu_app
