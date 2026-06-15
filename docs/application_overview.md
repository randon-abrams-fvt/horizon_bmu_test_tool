# BMU Test Tool — Application Overview

This document describes the BMU Test Tool: how it starts, how the UI is laid out,
and which pieces of code own the runtime behavior. It covers both subsystems:

1. The **proto-message TCP client** that talks to the BMU (Overview tab).
2. The **CANopen master/monitor** for the TTC 2038XS safety I/O module
   (Components tab).

> For a deep, driver-oriented description of the TTC 2038XS device protocol, see
> [ttc2038xs_device_guide.md](ttc2038xs_device_guide.md). For the rationale
> behind major changes, see [agent-decisions.md](agent-decisions.md).

## Startup path

The executable starts in `src/main.cpp`, which sets up the Win32 window,
DirectX 11 device, Dear ImGui, and ImPlot. It then creates `bmu_app::App` and
calls `App::render()` once per frame.

The app loads these runtime files relative to the executable (falling back to the
current working directory if not found):

- `bmu_messages.desc`
- `bmu_messages.yaml`
- `RobotoMono-Regular.ttf`

## Top-level layout

`src/App.cpp` owns the application. The main window is a borderless full-screen
host with a menu bar and a top-level tab bar with **two tabs**:

- **Overview** — the BMU proto-message client. A splitter puts Commands (left)
  and Status (right) above a Traffic region.
- **Components** — a nested tab bar of connected devices. Currently one entry:
  **TTC2038XS**.

The menu bar has **File** (Exit), **Connection** (Connect…/Disconnect for the
BMU TCP runtime), and **View** (Settings), plus a right-aligned connection
indicator.

## Subsystem 1 — BMU proto-message client

The runtime layer in `src/runtime/` wraps the vendored `message_runtime` /
`proto_messages` libraries:

- `BmuRuntime` owns the `ClientRuntime`, the dynamic protobuf codec, and the
  traffic/status buffers. It implements `ITrafficObserver`.
- `BmuRouter` decides whether injected messages are dropped, routed by
  destination, or broadcast.

UI panels under `src/ui/` (each renders inline into a tab — they do **not** call
`Begin`/`End` themselves):

- `CommandPanel` builds and sends `bmu.BmuCommand` messages (one-shot or cyclic).
- `StatusPanel` renders the latest `bmu.BmuStatus` snapshot.
- `TrafficPanel` summarizes proto traffic with per-type counts, cycle time, and
  payload.

Message types are defined in `proto/bmu_messages.proto` (`BmuStatus`,
`BmuCommand`).

## Subsystem 2 — CANopen (TTC 2038XS)

All CANopen code lives under `src/canopen/`. It is a **hand-written** CANopen
implementation (no external stack) talking to a PEAK PCAN-USB adapter.

Transport and protocol:

- `CanFrame` — adapter-independent CAN frame.
- `PcanBackend` (`PcanChannel`) — RAII wrapper over a single PCANBasic channel;
  `PCANBasic.dll` is resolved at runtime, so the tool builds/runs without the
  PEAK SDK (it reports "driver unavailable" when the DLL is missing). Supports
  listen-only mode.
- `CanOpenDefs` — COB-ID function codes, NMT commands/states, SDO command
  specifiers, abort-code text.
- `CanOpenClient` — owns one `PcanChannel` and a worker thread that pumps the
  bus. Provides NMT, a full SDO client (expedited + segmented up/download),
  heartbeat/node tracking, and per-COB-ID traffic statistics. Two modes:
  **Control** (active master) and **Monitor** (passive listen-only).

Device layer (`src/canopen/devices/`):

- `Ttc2038Xs.h/.cpp` — object-dictionary constants, the **pin configuration
  model** (`PinGroup` / `PinInfo` / `PinModeOption`, `pin_groups()`), the
  **I/O function model** (`IoFunction`, `ModeBehavior`, `mode_behavior()`), and
  SRDO pair validation.
- `Ttc2038XsDevice.h/.cpp` — a high-level wrapper around `CanOpenClient` for one
  node. Owns an **SDO job sequencer** (`poll()` services one transfer per
  frame), a curated parameter table, decoded device status, pin-mode and pin-I/O
  caches, and node discovery (passive inference + active SDO scan). No CANopen
  indices leak into the UI.

UI panel (`src/ui/Ttc2038XsPanel.cpp`) — one self-contained panel owning its own
`CanOpenClient`. Internal tabs:

- **Status** — identity, NMT, diagnostics, supply voltages, DI/AI snapshots,
  safety-switch status.
- **Control** — **dynamically generated from each pin's configured mode**: NMT
  controls, then one section per I/O function (Digital Output tiles, PWM/LPO
  duty-cycle writes, read-only Digital/Analog/Timer/SENT inputs), then Safe
  state / Diagnostics / Persistence.
- **Configure** — the curated parameter table (read/write known OD entries).
- **Pins** — per-pin **Pin Mode** read/write, grouped by I/O pin group with a
  mode dropdown per pin.
- **Advanced** — raw SDO access to any object-dictionary index/sub-index.
- **CAN Traffic** — live per-COB-ID statistics.

## Connection workflows

- **BMU (Overview):** the Connection menu opens a connect dialog (host/port).
  Once connected, the app can send a single command, send commands cyclically,
  and update the status/traffic panels from runtime events.
- **TTC (Components → TTC2038XS):** the panel's Connection section scans PCAN
  channels, picks a bitrate (default 500 kbit/s), a mode (Control/Monitor), and
  the device node-ID (device default 10), then connects. Control mode is
  required to read/write SDOs.

## Assets and runtime files

The executable expects these beside it (installed by `CMakeLists.txt`):

- `RobotoMono-Regular.ttf`
- `bmu_messages.desc`
- `bmu_messages.yaml`
- vcpkg runtime DLLs (protobuf, boost, spdlog, yaml-cpp, …)

`PCANBasic.dll` is **not** bundled; install the PEAK driver to use CAN.

## Build & run

Use the PowerShell scripts in `scripts/` (they import the MSVC x64 environment
and drive CMake presets):

- `scripts\config.ps1 [-BuildType debug|release]` — configure (bootstraps vcpkg
  on first run).
- `scripts\build.ps1 [-BuildType debug|release] [-Configure]` — build + install
  to `output/<buildType>/bin`.
- `scripts\run.ps1 [-BuildType debug|release]` — run the installed exe.
- `scripts\clean.ps1` — remove build artifacts.

> Note: the build's install step copies `BmuTestTool.exe` into
> `output/<buildType>/bin`. If the app is **running**, that copy fails with
> "Permission denied" — close it before rebuilding.

## Related files

- [src/main.cpp](../src/main.cpp)
- [src/App.cpp](../src/App.cpp)
- [src/runtime/BmuRuntime.h](../src/runtime/BmuRuntime.h)
- [src/runtime/BmuRouter.cpp](../src/runtime/BmuRouter.cpp)
- [src/ui/CommandPanel.cpp](../src/ui/CommandPanel.cpp)
- [src/ui/StatusPanel.cpp](../src/ui/StatusPanel.cpp)
- [src/ui/TrafficPanel.cpp](../src/ui/TrafficPanel.cpp)
- [src/ui/Ttc2038XsPanel.cpp](../src/ui/Ttc2038XsPanel.cpp)
- [src/canopen/CanOpenClient.cpp](../src/canopen/CanOpenClient.cpp)
- [src/canopen/PcanBackend.cpp](../src/canopen/PcanBackend.cpp)
- [src/canopen/devices/Ttc2038XsDevice.cpp](../src/canopen/devices/Ttc2038XsDevice.cpp)
