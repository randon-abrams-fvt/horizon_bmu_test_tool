#include "TrafficPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace bmu_app
{

void TrafficPanel::push_event(const BmuTrafficEvent &event)
{
    const std::string key =
        std::string(event.is_rx ? "RX:" : "TX:") + event.type_name;

    auto it = stats_.find(key);
    const auto now = Clock::now();

    if (it == stats_.end())
    {
        MessageStats ms;
        ms.type_name = event.type_name;
        ms.origin = event.origin;
        ms.s_id = event.s_id.has_value() ? std::to_string(*event.s_id) : "--";
        ms.d_id = event.d_id.has_value() ? std::to_string(*event.d_id) : "--";
        ms.is_rx = event.is_rx;
        ms.count = 1;
        ms.first_seen = now;
        ms.last_seen = now;
        ms.min_cycle_ms = 0.0;
        ms.max_cycle_ms = 0.0;
        ms.avg_cycle_ms = 0.0;
        ms.cycle_sum_ms = 0.0;
        ms.last_payload = event.raw_bytes;
        stats_[key] = ms;
        ordered_keys_.push_back(key);
    }
    else
    {
        auto &ms = it->second;
        const double delta_ms =
            std::chrono::duration<double, std::milli>(now - ms.last_seen)
                .count();

        ms.count++;
        ms.last_seen = now;

        if (ms.count == 2)
        {
            // First interval
            ms.min_cycle_ms = delta_ms;
            ms.max_cycle_ms = delta_ms;
            ms.cycle_sum_ms = delta_ms;
            ms.avg_cycle_ms = delta_ms;
        }
        else
        {
            ms.cycle_sum_ms += delta_ms;
            ms.avg_cycle_ms =
                ms.cycle_sum_ms / static_cast<double>(ms.count - 1);
            if (delta_ms < ms.min_cycle_ms)
                ms.min_cycle_ms = delta_ms;
            if (delta_ms > ms.max_cycle_ms)
                ms.max_cycle_ms = delta_ms;
        }
        ms.s_id = event.s_id.has_value() ? std::to_string(*event.s_id) : "--";
        ms.d_id = event.d_id.has_value() ? std::to_string(*event.d_id) : "--";
        ms.last_payload = event.raw_bytes;
    }
}

void TrafficPanel::clear()
{
    stats_.clear();
    ordered_keys_.clear();
}

void TrafficPanel::render()
{
    // Controls row
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Filter##traffic", filter_buf_, sizeof(filter_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        clear();
    }

    ImGui::Spacing();

    // Table
    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_Sortable;

    const ImVec2 table_size{0.0f, 0.0f}; // fill available

    if (!ImGui::BeginTable("##traffic_stats", 10, kTableFlags, table_size))
    {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Dir", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn(
        "Message Type", ImGuiTableColumnFlags_WidthFixed, 200.0f);
    ImGui::TableSetupColumn("Origin", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Src", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Dst", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn(
        "Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn(
        "Min (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn(
        "Max (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn(
        "Payload (hex)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();

    const std::string_view filter{filter_buf_};

    for (const auto &key : ordered_keys_)
    {
        auto it = stats_.find(key);
        if (it == stats_.end())
            continue;

        const auto &ms = it->second;

        // Apply filter
        if (!filter.empty() && ms.type_name.find(filter) == std::string::npos)
        {
            continue;
        }

        ImGui::TableNextRow();

        // Dir
        ImGui::TableSetColumnIndex(0);
        if (ms.is_rx)
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "RX");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "TX");

        // Type
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(ms.type_name.c_str());

        // Origin
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(ms.origin.c_str());

        // Src
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(ms.s_id.c_str());

        // Dst
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(ms.d_id.c_str());

        // Count
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%llu", static_cast<unsigned long long>(ms.count));

        // Avg cycle
        ImGui::TableSetColumnIndex(6);
        if (ms.count >= 2)
            ImGui::Text("%.1f", ms.avg_cycle_ms);
        else
            ImGui::TextDisabled("--");

        // Min cycle
        ImGui::TableSetColumnIndex(7);
        if (ms.count >= 2)
            ImGui::Text("%.1f", ms.min_cycle_ms);
        else
            ImGui::TextDisabled("--");

        // Max cycle
        ImGui::TableSetColumnIndex(8);
        if (ms.count >= 2)
            ImGui::Text("%.1f", ms.max_cycle_ms);
        else
            ImGui::TextDisabled("--");

        // Payload (hex)
        ImGui::TableSetColumnIndex(9);
        if (!ms.last_payload.empty())
        {
            // Build hex string from last received payload
            std::string hex;
            hex.reserve(ms.last_payload.size() * 3);
            for (size_t i = 0; i < ms.last_payload.size(); ++i)
            {
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%02X ", ms.last_payload[i]);
                hex += buf;
            }
            ImGui::TextUnformatted(hex.c_str(), hex.c_str() + hex.size());
        }
        else
        {
            ImGui::TextDisabled("--");
        }
    }

    ImGui::EndTable();
}

} // namespace bmu_app
