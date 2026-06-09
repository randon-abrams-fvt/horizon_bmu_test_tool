# BMU Test Tool Application Overview

This document describes the BMU Test Tool itself: how it starts, how the UI is arranged, and which pieces of code own the runtime and panel behavior.

## Startup path

The executable starts in `src/main.cpp`, which sets up the Win32 window, DirectX 11 device, Dear ImGui, and ImPlot. It then creates `bmu_app::App` and calls `App::render()` once per frame.

The app loads these runtime files relative to the executable:

- `bmu_messages.desc`
- `bmu_messages.yaml`

If the files are not found next to the executable, the code falls back to the current working directory.

## Main responsibilities

`src/App.cpp` is the top-level owner of the application state. It manages:

- the shared `BmuRuntime` instance
- connection and settings dialogs
- the panel layout and splitter
- menu actions for connect, disconnect, and settings

The runtime layer in `src/runtime/` provides the glue to `message_runtime`:

- `BmuRuntime` owns the client runtime, codec, and traffic/status buffers.
- `BmuRouter` decides whether simulated messages are dropped, routed by destination, or broadcast.

## UI panels

The app is split into three panels under `src/ui/`:

- `CommandPanel` builds and sends `BmuCommand` messages.
- `StatusPanel` renders the latest `BmuStatus` snapshot.
- `TrafficPanel` summarizes received and transmitted traffic with timing and payload details.

The top-level layout uses a borderless host window with a command/status row above a traffic region. A draggable horizontal splitter controls how much space the top section gets.

## Connection workflow

The Connection menu opens a connect dialog where the user supplies host and port values. Once connected, the app can:

- send a single command immediately
- send commands on a cyclic timer
- show session state in the command panel
- update the status and traffic panels from runtime events

Disconnect tears down the runtime and clears the live connection state.

## Assets and runtime files

The executable expects the following runtime assets to be deployed beside it:

- `RobotoMono-Regular.ttf`
- `bmu_messages.desc`
- `bmu_messages.yaml`

These are installed by the CMake rules in `CMakeLists.txt`.

## Related files

- [src/main.cpp](../src/main.cpp)
- [src/App.cpp](../src/App.cpp)
- [src/runtime/BmuRuntime.h](../src/runtime/BmuRuntime.h)
- [src/runtime/BmuRuntime.cpp](../src/runtime/BmuRuntime.cpp)
- [src/runtime/BmuRouter.cpp](../src/runtime/BmuRouter.cpp)
- [src/ui/CommandPanel.cpp](../src/ui/CommandPanel.cpp)
- [src/ui/StatusPanel.cpp](../src/ui/StatusPanel.cpp)
- [src/ui/TrafficPanel.cpp](../src/ui/TrafficPanel.cpp)