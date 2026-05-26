# Reference Guide — Building a New Simulator App from this Template

This document captures all the libraries, architecture patterns, UI conventions, and
styling decisions used in the BMU Simulator so that a new app with a different
communication protocol can be built to match the same look and feel.

---

## 1. Technology Stack

| Layer | Technology | Notes |
|---|---|---|
| **Language** | C++17 | STL threading, `std::optional`, structured bindings |
| **Build system** | CMake ≥ 3.20 with CMakePresets.json | Presets: `windows-Debug`, `windows-Release` |
| **GUI framework** | Dear ImGui (docking branch) | Immediate-mode UI, no layout files |
| **Plotting** | ImPlot | ImGui-native; scrolling time-series |
| **Renderer backend** | DirectX 11 + Win32 | `imgui_impl_win32.cpp` + `imgui_impl_dx11.cpp` |
| **Font** | Roboto Mono Regular, 20 px | Loaded via `io.Fonts->AddFontFromFileTTF` |

All third-party libraries live under `third_party/` and are compiled from source
(no system-installed packages required, except the communication SDK if it is external).

---

## 2. Project Structure to Replicate

```
CMakeLists.txt          – build definition, embeds any data files at configure time
CMakePresets.json       – windows-Debug / windows-Release presets
assets/
    fonts/              – RobotoMono-Regular.ttf (must be next to the .exe at runtime)
src/
    main.cpp            – Win32 window, DX11 device, ImGui/ImPlot init, render loop
    App.h / App.cpp     – top-level owner: owns model, bus, all panels
    <protocol>/         – replace the CAN layer with your protocol (see §4)
    ui/
        XxxPanel.h/.cpp – one panel class per tab
third_party/
    imgui/              – Dear ImGui (docking branch)
    implot/             – ImPlot
    <your protocol SDK or wrapper>
```

---

## 3. Entry Point Pattern (`main.cpp`)

The entry point follows the Dear ImGui `example_win32_directx11` sample almost verbatim.
Key setup decisions to preserve:

```cpp
// Window creation
HWND hwnd = CreateWindowW(..., L"Your App Title", WS_OVERLAPPEDWINDOW,
    100, 100, 1600, 900, ...);

// ImGui + ImPlot init
IMGUI_CHECKVERSION();
ImGui::CreateContext();
ImPlot::CreateContext();          // must come after ImGui::CreateContext()

// Feature flags
io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

// Font — load before first frame
constexpr float kBaseFontSize = 20.0f;
io.Fonts->AddFontFromFileTTF("RobotoMono-Regular.ttf", kBaseFontSize);

// Theme + scale
ImGui::StyleColorsDark();
constexpr float kUiScale = 1.15f;
ImGui::GetStyle().ScaleAllSizes(kUiScale);

// Background clear color (very dark grey)
constexpr float kClearColor[4] = { 0.10f, 0.10f, 0.10f, 1.0f };
```

The render loop calls `app.render()` once per frame between
`ImGui::NewFrame()` and `ImGui::Render()`. No other logic lives in `main.cpp`.

---

## 4. Protocol Abstraction Layer

The communication layer is hidden behind a pure-virtual interface.
Replace the CAN-specific types with your protocol's equivalents, but keep the same
structural pattern so panels stay protocol-agnostic.

### 4.1 Frame types (analogous to `CanFrame` / `DecodedFrame`)

```cpp
// Raw transport frame
struct MyFrame {
    // ... whatever your protocol uses (bytes, ID, timestamp, etc.)
    double timestampMs = 0.0;
};

// Decoded frame — raw bytes + extracted signal values
struct DecodedFrame {
    MyFrame                                     raw;
    std::string                                 messageName;
    std::vector<std::pair<std::string, double>> signals;  // {name, physical value}
};
```

### 4.2 Interface (analogous to `ICanInterface`)

```cpp
class IMyInterface {
public:
    virtual ~IMyInterface() = default;
    virtual bool        open(/* connection params */) = 0;
    virtual void        close()                        = 0;
    virtual bool        send(const MyFrame& frame)     = 0;
    virtual bool        receive(MyFrame& frameOut)     = 0;  // non-blocking
    virtual bool        isOpen()    const              = 0;
    virtual const char* lastError() const              = 0;
};
```

Provide at least two concrete implementations:
- **Real hardware** — wraps your actual SDK.
- **Virtual / loopback** — for development without hardware; echoes sent frames
  back to the RX queue.

### 4.3 Bus class (analogous to `CanBus`)

The bus class wraps the interface with a **background RX thread** and a
**mutex-guarded decoded-frame queue**. The UI thread calls `drainRxFrames()`
once per render frame.

```cpp
class MyBus {
public:
    explicit MyBus(std::unique_ptr<IMyInterface> iface);
    ~MyBus();
    bool open(/* params */);
    void close();
    bool isOpen() const;
    bool send(const MyFrame& frame);
    std::vector<DecodedFrame> drainRxFrames();   // UI thread only

private:
    void rxLoop();   // runs on rxThread_, calls iface_->receive() in a tight loop

    std::unique_ptr<IMyInterface> iface_;
    std::thread              rxThread_;
    std::atomic<bool>        running_{ false };
    std::mutex               rxMutex_;
    std::queue<DecodedFrame> rxQueue_;
};
```

Threading model:

```
Hardware → RX thread (blocks on receive) → rxQueue_ (mutex) → UI thread (drainRxFrames)
```

---

## 5. App Class Pattern

`App` is the single top-level object created in `main.cpp`. It owns:
- The model/config object (DBC, EDS, or whatever schema your protocol uses)
- The `MyBus` instance (created on connect, destroyed on disconnect)
- All UI panel instances

```cpp
class App {
public:
    App();
    ~App();
    void render();   // called once per ImGui frame

private:
    void renderMenuBar();
    void renderConnectWindow();
    void renderSettingsWindow();

    MyModel model_;
    std::unique_ptr<MyBus> bus_;

    // One member per panel
    BusMonitorPanel busMonitor_;
    SignalsPanel    signalsPanel_;
    FaultsPanel     faultsPanel_;
    PlotPanel       plotPanel_;

    // Modal dialog state
    bool showConnectWindow_  = false;
    bool showSettingsWindow_ = false;

    // Settings
    float textScale_    = 1.0f;
    int   themeIdx_     = 0;    // 0=Dark, 1=Light, 2=Classic
    bool  dockingEnabled_ = true;
    bool  showDemoWindow_ = false;
};
```

---

## 6. Host Window + Tab Bar Layout

The application uses a **borderless full-screen ImGui window** as a host for a
top-level tab bar. This avoids a floating empty root window.

```cpp
void App::render()
{
    // Full-screen host
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    constexpr ImGuiWindowFlags kHostFlags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoCollapse   |
        ImGuiWindowFlags_NoResize      | ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##Host", nullptr, kHostFlags);
    ImGui::PopStyleVar(2);

    renderMenuBar();

    // Drain frames and distribute to panels
    if (bus_ && bus_->isOpen()) {
        auto frames = bus_->drainRxFrames();
        for (const auto& f : frames) {
            busMonitor_.pushFrame(f);
            signalsPanel_.pushFrame(f);
            // ... other panels
        }
    }

    // Tab bar
    if (ImGui::BeginTabBar("##MainTabs")) {
        if (ImGui::BeginTabItem("Bus Monitor"))  { busMonitor_.render();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Signals"))      { signalsPanel_.render(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Plots"))        { plotPanel_.render();    ImGui::EndTabItem(); }
        // ... add more tabs as needed
        ImGui::EndTabBar();
    }

    ImGui::End();

    if (showConnectWindow_)  renderConnectWindow();
    if (showSettingsWindow_) renderSettingsWindow();
}
```

---

## 7. Menu Bar Conventions

The menu bar follows this structure:

```
File | <Protocol> | View                              [status text right-aligned]
```

- **File** → Exit (calls `PostQuitMessage(0)`)
- **\<Protocol\>** → Connect… / Disconnect (toggles modal dialog)
- **View** → Settings (toggles settings modal)
- **Right-aligned status** — connection state + active schema file name, rendered with
  `ImGui::TextDisabled` at `GetContentRegionMax().x - calculatedWidth`.

```cpp
// Right-align pattern
float rightWidth = ImGui::CalcTextSize(statusA).x
                 + ImGui::CalcTextSize("  ").x
                 + ImGui::CalcTextSize(statusB).x + 16.0f;
ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - rightWidth);
ImGui::TextDisabled("%s  %s", statusA, statusB);
```

---

## 8. Panel Interface Contract

Every panel follows the same three-method contract:

```cpp
class XxxPanel {
public:
    // Called once to inject dependencies (model, bus)
    void setModel(const MyModel* model);
    void setBus(MyBus* bus);

    // Called by App::render() for every drained frame (UI thread)
    void pushFrame(const DecodedFrame& frame);

    // Called by App::render() inside BeginTabItem / EndTabItem
    // Must NOT call Begin/End itself — renders inline into the current window
    void render();
};
```

`pushFrame` updates internal live-value maps. `render` reads from them.
Panels **do not own the bus or model** — they hold raw non-owning pointers.

---

## 9. Standard Panels

### Bus Monitor Panel
- **Purpose**: Show all raw traffic and per-message statistics.
- **Layout**: Fixed-width left child (`ImGui::BeginChild`) for per-ID stats table;
  remaining width for scrolling log table.
- **Stats table columns**: ID (hex), Name, Count, Hz (rolling rate).
- **Log table columns**: Time (ms), ID (hex), Name, DLC, Data (hex bytes).
- **Log cap**: 2000 entries (oldest dropped).
- **Rate update**: recalculated every 500 ms from a count snapshot.
- **Controls**: "Auto-scroll" checkbox top-right; filter text box above log.

### Signals Panel (formerly System Panel)
- **Layout**: Two-column `ImGui::Columns` or `BeginChild` split — TX controls on the
  left, received signal values on the right.
- **TX controls**: Checkboxes / sliders for command fields, "Send" button, optional
  cyclic transmit with a Hz/ms input and enable checkbox.
- **RX display**: Group signals by message name with `ImGui::SeparatorText`;
  show name + current value + unit.
- **Live value tracking**: `std::unordered_map<std::string, LiveVal>` where
  `LiveVal = { double value; bool received; }`. Display `--` until first frame.

### Fault Panel
- **Purpose**: Boolean status grid for diagnostic/fault bits.
- **Layout**: Full-width grid of colored buttons or colored `ImGui::Text` blocks.
- **Color coding**: Active fault → `ImVec4(1.0f, 0.3f, 0.3f, 1.0f)` (red);
  inactive → `ImVec4(0.2f, 0.7f, 0.2f, 1.0f)` (green).
- **Waiting state**: Show `ImGui::TextDisabled("Waiting for first frame...")` until
  the relevant message has been received at least once.

### Plot Panel
- **Purpose**: Scrolling time-series of user-selected signals.
- **Layout**: Fixed-width left child (≈220 px) with filterable checkbox list of all
  available signals; right child with the ImPlot graph.
- **Signal list**: `ImGui::InputText` filter box at the top, then `ImGui::Checkbox`
  per signal. Signal labels formatted as `"MessageName.SignalName"`.
- **Data storage**: `std::deque<double>` for time and value per selected signal,
  capped at 10 000 points.
- **ImPlot setup**:

```cpp
if (ImPlot::BeginPlot("##Signals", ImVec2(-1, -1))) {
    ImPlot::SetupAxes("Time (s)", "Value");
    ImPlot::SetupAxisLimits(ImAxis_X1, tMax - windowSec, tMax, ImGuiCond_Always);
    for (auto& [label, trace] : traces_) {
        if (!trace.times.empty())
            ImPlot::PlotLine(label.c_str(), trace.times.data(),
                             trace.values.data(), (int)trace.times.size());
    }
    ImPlot::EndPlot();
}
```

---

## 10. Connection Dialog

A fixed-size modal-style floating window (`ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse`):

```cpp
ImGui::SetNextWindowSize(ImVec2(400.0f, 220.0f), ImGuiCond_Appearing);
ImGui::Begin("Connect", &showConnectWindow_, ImGuiWindowFlags_NoResize | ...);
```

Contents:
1. `ImGui::Combo` to choose interface type (e.g., "Virtual", "Hardware").
2. If hardware selected: device/port picker combo, rescan button, baud rate / config combo.
3. Error feedback with `ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "error msg")`.
4. `ImGui::Button("Connect")` at the bottom — calls `connectBus()` and closes window on success.

---

## 11. Settings Window

A small floating window toggled from View → Settings:

| Setting | Widget |
|---|---|
| Text scale | `ImGui::SliderFloat("Text Scale", &textScale_, 0.5f, 2.0f)` |
| Theme | `ImGui::Combo` with Dark / Light / Classic |
| Show ImGui Demo | `ImGui::Checkbox` |
| Show Metrics | `ImGui::Checkbox` |

Apply theme immediately on change:

```cpp
switch (themeIdx_) {
    case 0: ImGui::StyleColorsDark();    break;
    case 1: ImGui::StyleColorsLight();   break;
    case 2: ImGui::StyleColorsClassic(); break;
}
```

---

## 12. Styling Reference

| Property | Value |
|---|---|
| Background clear color | `(0.10, 0.10, 0.10, 1.0)` — very dark grey |
| Default theme | `ImGui::StyleColorsDark()` |
| UI scale multiplier | `1.15f` applied via `ScaleAllSizes` |
| Font | Roboto Mono Regular, 20 px base size |
| Host window rounding | `0.0f` (square edges, full-screen) |
| Host window border | `0.0f` |
| Error / warning text | `ImVec4(1.0f, 0.4f, 0.4f, 1.0f)` (soft red) |
| Active fault | `ImVec4(1.0f, 0.3f, 0.3f, 1.0f)` |
| Inactive / OK | `ImVec4(0.2f, 0.7f, 0.2f, 1.0f)` |
| Disabled / placeholder text | `ImGui::TextDisabled(...)` |

---

## 13. CMakeLists.txt Skeleton

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApp LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── ImGui (Win32 + DirectX 11) ───────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ── ImPlot ───────────────────────────────────────────────────────────────────
set(IMPLOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot)
add_library(implot STATIC
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${IMPLOT_DIR})
target_link_libraries(implot PUBLIC imgui)

# ── Your protocol SDK (example: static lib) ──────────────────────────────────
# add_subdirectory(third_party/your_sdk) or link a pre-built .lib

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(MyApp WIN32
    src/main.cpp
    src/App.cpp
    src/<protocol>/MyBus.cpp
    src/<protocol>/RealInterface.cpp
    src/<protocol>/VirtualInterface.cpp
    src/ui/BusMonitorPanel.cpp
    src/ui/SignalsPanel.cpp
    src/ui/FaultPanel.cpp
    src/ui/PlotPanel.cpp
)
target_include_directories(MyApp PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(MyApp PRIVATE imgui implot d3d11 dxgi)
```

Add a `CMakePresets.json` with `windows-Debug` and `windows-Release` configure/build
preset pairs, using the MinGW or MSVC toolchain as appropriate.

---

## 14. Schema / Model Embedding (Optional)

If your protocol uses a configuration or schema file (like a DBC, EDS, or JSON), embed
it at CMake configure time so the binary is self-contained:

```cmake
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/my_schema.json" SCHEMA_CONTENT)
file(CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedSchema.h"
    CONTENT
"#pragma once
namespace EmbeddedSchema {
inline constexpr char kContent[] = R\"SCHEMA(${SCHEMA_CONTENT})SCHEMA\";
}
")
```

Then `#include "EmbeddedSchema.h"` and pass `EmbeddedSchema::kContent` to your model
loader on startup — no file selection dialog required.

---

## 15. What to Replace vs. What to Keep

| Keep as-is | Replace / adapt |
|---|---|
| `main.cpp` Win32/DX11 boilerplate | Protocol SDK init/teardown |
| ImGui + ImPlot library source | Protocol frame types (`MyFrame`, `DecodedFrame`) |
| Full-screen host window pattern | `IMyInterface` and concrete implementations |
| Tab bar navigation structure | `MyBus` RX thread logic |
| Panel interface contract | Schema/model parsing logic |
| Menu bar layout & right-aligned status | Connect dialog fields (ports, baud, etc.) |
| Styling, font, colors | Panel content (signals, faults specific to new protocol) |
| Settings window | — |
| Bus Monitor Panel (only column names change) | — |
| Plot Panel (protocol-agnostic already) | — |
