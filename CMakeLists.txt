cmake_minimum_required(VERSION 3.24)
project(BmuTestTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── vcpkg / package detection ─────────────────────────────────────────────────
# The toolchain already ran vcpkg integration; just find the packages.

# Disable sub-library options we don't need from proto_messages
set(BUILD_NANO_MESSAGES_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_PROTO_EXAMPLE_APPS OFF CACHE BOOL "" FORCE)
set(BUILD_MESSAGE_RUNTIME_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_MESSAGE_RUNTIME_TOOLS OFF CACHE BOOL "" FORCE)
set(NO_INSTALL ON CACHE BOOL "" FORCE)

# ── message_runtime + proto_messages (from libs/) ────────────────────────────
if(NOT TARGET message_runtime)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/message_runtime"
        "${CMAKE_BINARY_DIR}/libs/message_runtime"
    )
endif()

# ── Dear ImGui (docking branch) ───────────────────────────────────────────────
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui")
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR
        "Dear ImGui not found at ${IMGUI_DIR}.\n"
        "Clone it with:\n"
        "  git submodule add -b docking https://github.com/ocornut/imgui.git libs/imgui\n"
        "  git submodule update --init --recursive")
endif()

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ── ImPlot ────────────────────────────────────────────────────────────────────
set(IMPLOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/implot")
if(NOT EXISTS "${IMPLOT_DIR}/implot.h")
    message(FATAL_ERROR
        "ImPlot not found at ${IMPLOT_DIR}.\n"
        "Clone it with:\n"
        "  git submodule add https://github.com/epezent/implot.git libs/implot\n"
        "  git submodule update --init --recursive")
endif()

add_library(implot STATIC
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${IMPLOT_DIR})
target_link_libraries(implot PUBLIC imgui)

# ── Protobuf descriptor generation ───────────────────────────────────────────
find_package(Protobuf CONFIG REQUIRED)

set(BMU_PROTO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/proto/bmu_messages.proto")
set(BMU_DESC_OUT  "${CMAKE_CURRENT_BINARY_DIR}/proto/bmu_messages.desc")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/proto")

add_custom_command(
    OUTPUT  "${BMU_DESC_OUT}"
    COMMAND "$<TARGET_FILE:protobuf::protoc>"
            "-I" "${CMAKE_CURRENT_SOURCE_DIR}/proto"
            "--descriptor_set_out=${BMU_DESC_OUT}"
            "--include_imports"
            "${BMU_PROTO_SRC}"
    DEPENDS "${BMU_PROTO_SRC}"
    COMMENT "Generating bmu_messages.desc"
)

add_custom_target(bmu_proto_desc ALL DEPENDS "${BMU_DESC_OUT}")

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(BmuTestTool WIN32
    src/main.cpp
    src/App.cpp
    src/runtime/BmuRouter.cpp
    src/runtime/BmuRuntime.cpp
    src/ui/StatusPanel.cpp
    src/ui/CommandPanel.cpp
    src/ui/TrafficPanel.cpp
    src/ui/Ttc2038XsPanel.cpp
    src/canopen/PcanBackend.cpp
    src/canopen/CanOpenDefs.cpp
    src/canopen/CanOpenClient.cpp
    src/canopen/devices/Ttc2038Xs.cpp
    src/canopen/devices/Ttc2038XsDevice.cpp
)

add_dependencies(BmuTestTool bmu_proto_desc)

target_compile_features(BmuTestTool PRIVATE cxx_std_20)

target_include_directories(BmuTestTool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(BmuTestTool PRIVATE
    message_runtime
    imgui
    implot
    d3d11
    dxgi
)

if(WIN32)
    target_compile_definitions(BmuTestTool PRIVATE
        _WIN32_WINNT=0x0601
        WINVER=0x0601
        UNICODE
        _UNICODE
    )
endif()

if(MSVC)
    target_compile_options(BmuTestTool PRIVATE
        $<$<CONFIG:RELEASE>:/O2>
        $<$<CONFIG:DEBUG>:/MDd>
        $<$<CONFIG:DEBUG>:/Zi>
        $<$<CONFIG:DEBUG>:/Od>
    )
endif()

# ── Install: exe + runtime assets ────────────────────────────────────────────
install(TARGETS BmuTestTool
    RUNTIME DESTINATION bin
)

# Copy proto descriptor and YAML config next to the executable
install(FILES
    "${BMU_DESC_OUT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/bmu_messages.yaml"
    DESTINATION bin
)

# Copy font asset next to the executable
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/"
    DESTINATION bin
    FILES_MATCHING PATTERN "*.ttf"
)

# On Windows with shared libs, copy vcpkg runtime DLLs
if(WIN32)
    install(CODE "
        set(_vcpkg_root \"${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}\")
        if(CMAKE_INSTALL_CONFIG_NAME MATCHES \"^[Dd]ebug$\")
            set(_vcpkg_bin_dir \"\${_vcpkg_root}/debug/bin\")
        else()
            set(_vcpkg_bin_dir \"\${_vcpkg_root}/bin\")
        endif()
        if(EXISTS \"\${_vcpkg_bin_dir}\")
            file(GLOB _dlls \"\${_vcpkg_bin_dir}/*.dll\")
            if(_dlls)
                file(INSTALL DESTINATION \"${CMAKE_INSTALL_PREFIX}/bin\"
                    TYPE SHARED_LIBRARY FILES \${_dlls})
            endif()
        endif()
    ")
endif()
