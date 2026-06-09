# BMU Test Tool

BMU Test Tool is a Windows desktop application for connecting to a BMU, sending `BmuCommand` messages, and inspecting live `BmuStatus` and traffic activity.

The app uses C++20, Dear ImGui, ImPlot, Win32, and DirectX 11. Runtime schema data is loaded from `bmu_messages.desc` and `bmu_messages.yaml` next to the executable.

## What it does

- Connects to a BMU endpoint over the shared `message_runtime` transport layer.
- Sends one-shot or cyclic `BmuCommand` messages.
- Displays the latest `BmuStatus` snapshot.
- Shows a traffic table with message direction, type, origin, counts, timing, and payload bytes.

## Layout

- `src/main.cpp` contains the Win32 and DirectX 11 entry point.
- `src/App.cpp` owns the top-level UI, connection workflow, and panel layout.
- `src/runtime/` contains the BMU routing and runtime glue.
- `src/ui/` contains the command, status, and traffic panels.

## Build and run

The project is configured for CMake presets on Windows.

1. Configure with the `windows-Debug` or `windows-Release` preset.
2. Build the `BmuTestTool` target.
3. Run the installed executable from the configured output directory.

The repository also includes VS Code tasks for configure, build, clean, and run.

## Notes

- The font asset `RobotoMono-Regular.ttf` must be available next to the executable.
- Generated build artifacts live under `build/` and installed runtime outputs under `output/`.
- The app is intended to be used with the protocol descriptors in `proto/`.