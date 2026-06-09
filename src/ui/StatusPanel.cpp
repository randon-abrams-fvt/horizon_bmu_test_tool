#include "StatusPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cstdio>

namespace bmu_app
{

namespace
{
constexpr ImVec4 kColorClosed{0.20f, 0.70f, 0.20f, 1.0f};  // green bg
constexpr ImVec4 kColorOpen{0.35f, 0.35f, 0.35f, 1.0f};    // grey bg
constexpr ImVec4 kColorDotOn{1.00f, 0.20f, 0.20f, 1.0f};   // red dot
constexpr ImVec4 kColorDotOff{0.50f, 0.50f, 0.50f, 0.60f}; // dim grey dot
} // namespace

void StatusPanel::render()
{
    if (!snapshot_.received)
    {
        ImGui::TextDisabled("Waiting for first BmuStatus frame...");
        return;
    }

    // --- HSM State -----------------------------------------------------------
    ImGui::Text("HSM State: %u", snapshot_.hsm_state);

    // --- Source / Destination ------------------------------------------------
    if (snapshot_.s_id.has_value())
    {
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::Text("s_id: %u", *snapshot_.s_id);
    }
    if (snapshot_.d_id.has_value())
    {
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::Text("d_id: %u", *snapshot_.d_id);
    }

    ImGui::Spacing();

    // --- Digital Outputs -----------------------------------------------------
    ImGui::SeparatorText("Digital Outputs");
    render_digital_output_grid();
}

void StatusPanel::render_digital_output_grid()
{
    struct DoEntry
    {
        const char *label;
        bool request; // commanded state
        bool status;  // actual state
    };

    const std::array<DoEntry, 8> entries = {{
        {"DO0", snapshot_.do0_request, snapshot_.do0_status},
        {"DO1", snapshot_.do1_request, snapshot_.do1_status},
        {"DO2", snapshot_.do2_request, snapshot_.do2_status},
        {"DO3", snapshot_.do3_request, snapshot_.do3_status},
        {"DO4", snapshot_.do4_request, snapshot_.do4_status},
        {"DO5", snapshot_.do5_request, snapshot_.do5_status},
        {"DO6", snapshot_.do6_request, snapshot_.do6_status},
        {"DO7", snapshot_.do7_request, snapshot_.do7_status},
    }};

    constexpr float kCellW = 70.0f;
    constexpr float kCellH = 50.0f;
    constexpr float kDotRadius = 4.0f;
    constexpr float kDotMargin = 6.0f;
    constexpr float kSpacing = 6.0f;

    ImDrawList *draw_list = ImGui::GetWindowDrawList();

    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (i > 0)
            ImGui::SameLine(0.0f, kSpacing);

        const auto &e = entries[i];

        // Background color: green if status==closed (true), grey otherwise
        const ImVec4 bg = e.status ? kColorClosed : kColorOpen;
        const ImU32 bg_u32 = ImGui::ColorConvertFloat4ToU32(bg);

        const ImVec2 cursor = ImGui::GetCursorScreenPos();

        // Draw filled rectangle for the cell
        draw_list->AddRectFilled(
            cursor,
            ImVec2(cursor.x + kCellW, cursor.y + kCellH),
            bg_u32,
            4.0f); // corner rounding

        // Draw border
        draw_list->AddRect(
            cursor,
            ImVec2(cursor.x + kCellW, cursor.y + kCellH),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.6f, 0.6f, 0.8f)),
            4.0f);

        // Red dot in top-right corner for commanded state
        {
            const ImVec2 dot_center{
                cursor.x + kCellW - kDotMargin, cursor.y + kDotMargin};
            const ImVec4 dot_color = e.request ? kColorDotOn : kColorDotOff;
            draw_list->AddCircleFilled(
                dot_center,
                kDotRadius,
                ImGui::ColorConvertFloat4ToU32(dot_color));
        }

        // Label centered in the cell
        {
            const ImVec2 text_size = ImGui::CalcTextSize(e.label);
            const float tx = cursor.x + (kCellW - text_size.x) * 0.5f;
            const float ty = cursor.y + (kCellH - text_size.y) * 0.5f;
            draw_list->AddText(ImVec2(tx, ty), IM_COL32_WHITE, e.label);
        }

        // Advance cursor past this cell
        ImGui::Dummy(ImVec2(kCellW, kCellH));
    }
}

} // namespace bmu_app
