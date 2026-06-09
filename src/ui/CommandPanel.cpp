#include "CommandPanel.h"

#include <imgui.h>

#include <chrono>
#include <cstdio>

namespace bmu_app
{

void CommandPanel::render()
{
    const bool connected = runtime_ && runtime_->is_running();

    ImGui::BeginDisabled(!connected);

    // BMU node ID (destination)
    ImGui::Text("BMU Node ID (d_id)");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputScalar("##bmu_node_id", ImGuiDataType_U32, &bmu_node_id_);

    ImGui::Spacing();

    // Command fields
    ImGui::Checkbox("hv_connect", &hv_connect_);
    ImGui::Checkbox("hvil_closed", &hvil_closed_);

    ImGui::Spacing();

    // One-shot send
    if (ImGui::Button("Send", ImVec2(80.0f, 0.0f)))
    {
        if (try_send_command())
            last_send_status_ = "Sent OK";
        else
            last_send_status_ =
                "Send failed: " +
                (runtime_ ? runtime_->last_error() : "no runtime");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Cyclic transmit
    render_cyclic_controls();

    ImGui::EndDisabled();

    if (!last_send_status_.empty())
    {
        const bool is_error = last_send_status_.rfind("Send failed", 0) == 0;
        const ImVec4 color = is_error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                      : ImVec4(0.2f, 0.7f, 0.2f, 1.0f);
        ImGui::TextColored(color, "%s", last_send_status_.c_str());
    }

    if (!connected)
    {
        ImGui::TextDisabled("(not connected)");
    }

    // ── Connection ────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Connection");

    if (!connected)
    {
        ImGui::TextDisabled("Not connected.");
    }
    else
    {
        const auto state = runtime_->session_state();
        char node_buf[16] = "--";
        if (state.node_id.has_value())
        {
            std::snprintf(node_buf, sizeof(node_buf), "%u", *state.node_id);
        }
        char session_buf[24] = "--";
        if (state.session_id.has_value())
        {
            std::snprintf(
                session_buf,
                sizeof(session_buf),
                "%llu",
                static_cast<unsigned long long>(*state.session_id));
        }
        ImGui::Text(
            "Session: %-20s  Node: %-10s  %s",
            session_buf,
            node_buf,
            state.connected ? "Connected" : "Disconnected");
    }
}

void CommandPanel::render_cyclic_controls()
{
    ImGui::Text("Cyclic Transmit");
    ImGui::Checkbox("Enable##cyclic", &cyclic_enabled_);

    if (cyclic_enabled_)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderFloat("Hz##cyclic", &cyclic_hz_, 0.1f, 50.0f, "%.1f");

        // Accumulate time and fire when period elapsed.
        const double now_s =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count()) /
            1e6;

        if (last_frame_time_s_ == 0.0)
        {
            last_frame_time_s_ = now_s;
        }

        cyclic_accum_s_ += (now_s - last_frame_time_s_);
        last_frame_time_s_ = now_s;

        const double period_s = 1.0 / static_cast<double>(cyclic_hz_);
        if (cyclic_accum_s_ >= period_s)
        {
            cyclic_accum_s_ -= period_s;
            try_send_command();
        }
    }
    else
    {
        last_frame_time_s_ = 0.0;
        cyclic_accum_s_ = 0.0;
    }
}

bool CommandPanel::try_send_command()
{
    if (!runtime_)
    {
        return false;
    }
    return runtime_->send_bmu_command(bmu_node_id_, hv_connect_, hvil_closed_);
}

} // namespace bmu_app
