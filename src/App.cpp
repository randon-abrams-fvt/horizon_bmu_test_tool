#include "App.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <imgui.h>

#include <filesystem>
#include <string>

namespace bmu_app
{

namespace
{

// Resolve a file relative to the executable directory, then the CWD.
std::string resolve_asset_path(const char *filename)
{
    // Try next to the executable (populated at install time).
    std::error_code ec;
    const auto exe_dir =
        std::filesystem::current_path(ec); // start with CWD as fallback

    for (const auto &base : {std::filesystem::path("."), exe_dir})
    {
        const auto candidate = (base / filename).lexically_normal();
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate.string();
        }
    }

    return filename; // let the caller handle the error
}

} // namespace

App::App()
{
    desc_path_ = resolve_asset_path("bmu_messages.desc");
    yaml_path_ = resolve_asset_path("bmu_messages.yaml");

    status_panel_.set_runtime(&runtime_);
    command_panel_.set_runtime(&runtime_);
}

App::~App()
{
    disconnect();
}

void App::render()
{
    // Full-screen borderless host window
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    constexpr ImGuiWindowFlags kHostFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##Host", nullptr, kHostFlags);
    ImGui::PopStyleVar(2);

    render_menu_bar();

    // Drain traffic events and distribute to panels
    {
        auto events = runtime_.drain_traffic_events();
        for (auto &ev : events)
        {
            traffic_panel_.push_event(ev);
        }
    }

    // Push latest status snapshot to the status panel
    status_panel_.push_status(runtime_.latest_status());

    // ── Single overview layout ──────────────────────────────────────────────
    const float avail_h = ImGui::GetContentRegionAvail().y;
    constexpr float kSplitterThickness = 6.0f;
    const float upper_h = avail_h * upper_ratio_;

    // ---------- Upper region: Commands (left) | Status (right) -----------
    ImGui::BeginChild("##upper", ImVec2(0.0f, upper_h), false);
    {
        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float left_w = avail_w * 0.40f;

        // Upper-left: Commands
        ImGui::BeginChild("##cmd_region", ImVec2(left_w, 0.0f), true);
        ImGui::SeparatorText("Commands");
        command_panel_.render();
        ImGui::EndChild();

        ImGui::SameLine();

        // Upper-right: Status
        ImGui::BeginChild("##status_region", ImVec2(0.0f, 0.0f), true);
        ImGui::SeparatorText("Status");
        status_panel_.render();
        ImGui::EndChild();
    }
    ImGui::EndChild();

    // ---------- Draggable horizontal splitter ────────────────────────────
    {
        ImGui::InvisibleButton(
            "##hsplitter", ImVec2(-1.0f, kSplitterThickness));
        if (ImGui::IsItemActive())
        {
            float delta = ImGui::GetIO().MouseDelta.y;
            upper_ratio_ += delta / avail_h;
            if (upper_ratio_ < 0.15f)
                upper_ratio_ = 0.15f;
            if (upper_ratio_ > 0.85f)
                upper_ratio_ = 0.85f;
        }

        // Visual feedback: highlight when hovered or dragged
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetItemRectMin();
            ImVec2 sz = ImGui::GetItemRectSize();
            dl->AddRectFilled(
                p,
                ImVec2(p.x + sz.x, p.y + sz.y),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 0.4f)));
        }
    }

    // ---------- Bottom region: Traffic ───────────────────────────────────
    ImGui::BeginChild("##traffic_region", ImVec2(0.0f, 0.0f), true);
    ImGui::SeparatorText("Traffic");
    traffic_panel_.render();
    ImGui::EndChild();

    ImGui::End();

    if (show_connect_window_)
    {
        render_connect_window();
    }
    if (show_settings_window_)
    {
        render_settings_window();
    }
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void App::render_menu_bar()
{
    if (!ImGui::BeginMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Exit"))
        {
            PostQuitMessage(0);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Connection"))
    {
        const bool running = runtime_.is_running();
        if (!running && ImGui::MenuItem("Connect..."))
        {
            connect_error_.clear();
            show_connect_window_ = true;
        }
        if (running && ImGui::MenuItem("Disconnect"))
        {
            disconnect();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Settings"))
        {
            show_settings_window_ = !show_settings_window_;
        }
        ImGui::EndMenu();
    }

    // Right-aligned connection status
    {
        const bool running = runtime_.is_running();
        const char *state = running ? "Connected" : "Disconnected";
        const float right_width = ImGui::CalcTextSize("Status:").x +
                                  ImGui::CalcTextSize("  ").x +
                                  ImGui::CalcTextSize(state).x + 16.0f;

        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - right_width);
        ImGui::TextDisabled("Status:  ");
        ImGui::SameLine();
        if (running)
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "%s", state);
        else
            ImGui::TextDisabled("%s", state);
    }

    ImGui::EndMenuBar();
}

// ---------------------------------------------------------------------------
// Connect window
// ---------------------------------------------------------------------------

void App::render_connect_window()
{
    ImGui::SetNextWindowSize(ImVec2(380.0f, 200.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("Connect##window", &show_connect_window_, kFlags))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Host");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##host", host_buf_, sizeof(host_buf_));

    ImGui::Text("Port");
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("##port", &port_);
    if (port_ < 1)
        port_ = 1;
    if (port_ > 65535)
        port_ = 65535;

    ImGui::Spacing();

    if (ImGui::Button("Connect", ImVec2(120.0f, 0.0f)))
    {
        connect();
        if (runtime_.is_running())
        {
            show_connect_window_ = false;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
    {
        show_connect_window_ = false;
        connect_error_.clear();
    }

    if (!connect_error_.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(
            ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", connect_error_.c_str());
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Settings window
// ---------------------------------------------------------------------------

void App::render_settings_window()
{
    ImGui::SetNextWindowSize(ImVec2(320.0f, 180.0f), ImGuiCond_Appearing);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("Settings##window", &show_settings_window_, kFlags))
    {
        ImGui::End();
        return;
    }

    if (ImGui::SliderFloat("Text Scale", &text_scale_, 0.5f, 2.0f))
    {
        ImGui::GetIO().FontGlobalScale = text_scale_;
    }

    const char *themes[] = {"Dark", "Light", "Classic"};
    if (ImGui::Combo("Theme", &theme_idx_, themes, 3))
    {
        switch (theme_idx_)
        {
        case 0:
            ImGui::StyleColorsDark();
            break;
        case 1:
            ImGui::StyleColorsLight();
            break;
        case 2:
            ImGui::StyleColorsClassic();
            break;
        default:
            break;
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Connect / Disconnect helpers
// ---------------------------------------------------------------------------

void App::connect()
{
    BmuRuntimeConfig cfg;
    cfg.host = host_buf_;
    cfg.port = static_cast<uint16_t>(port_);
    cfg.yaml_path = yaml_path_;
    cfg.desc_path = desc_path_;

    if (!runtime_.start(cfg))
    {
        connect_error_ = "Failed to start: " + runtime_.last_error();
    }
}

void App::disconnect()
{
    runtime_.stop();
}

} // namespace bmu_app
