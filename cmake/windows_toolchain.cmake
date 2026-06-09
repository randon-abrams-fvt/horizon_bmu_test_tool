cmake_minimum_required(VERSION 3.24)

get_filename_component(_app_source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "")
endif()

# Resolve VCPKG_ROOT: env var > explicit cache > proto_messages' bundled vcpkg
if(NOT DEFINED VCPKG_ROOT AND DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH "")
endif()

if(NOT DEFINED VCPKG_ROOT)
    # Fall back to the vcpkg bootstrapped inside the proto_messages subproject
    set(VCPKG_ROOT "${_app_source_dir}/libs/proto_messages/vcpkg" CACHE PATH "")
endif()

# Bootstrap vcpkg if needed using proto_messages' bootstrap script
if(NOT EXISTS "${VCPKG_ROOT}/.vcpkg-root")
    if(EXISTS "${_app_source_dir}/libs/proto_messages/cmake/bootstrap_vcpkg.cmake")
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DVCPKG_ROOT=${VCPKG_ROOT}"
                -P "${_app_source_dir}/libs/proto_messages/cmake/bootstrap_vcpkg.cmake"
            RESULT_VARIABLE _vcpkg_bootstrap_result
            COMMAND_ECHO STDOUT
        )
        if(NOT _vcpkg_bootstrap_result EQUAL 0)
            message(FATAL_ERROR "Failed to bootstrap vcpkg at ${VCPKG_ROOT}")
        endif()
    else()
        message(FATAL_ERROR
            "VCPKG_ROOT is not set and proto_messages bootstrap script not found.\n"
            "Set the VCPKG_ROOT environment variable to your vcpkg installation.")
    endif()
endif()

# Point the manifest at this app's vcpkg.json (not proto_messages')
set(VCPKG_MANIFEST_DIR "${_app_source_dir}" CACHE PATH "")

if(DEFINED VCPKG_ROOT AND EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    include("${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
else()
    message(STATUS "VCPKG_ROOT is not configured; configure will rely on default package search paths.")
endif()
