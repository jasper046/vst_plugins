cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# Code of the Geeks - VST3 plugin family
#
# Top-level project. Pulls in the Steinberg VST3 SDK (git submodule under
# external/vst3sdk), the shared DSP library (common/), and each plugin under
# plugins/. Add a new plugin by dropping a subdirectory in plugins/ and an
# add_subdirectory() call at the bottom of this file.
# ---------------------------------------------------------------------------

# Path the SDK's own CMake expects. Must be set before add_subdirectory().
set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk")
if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found at ${vst3sdk_SOURCE_DIR}. "
        "Run: git submodule update --init --recursive")
endif()

project(cotg_vst_plugins
    VERSION 0.1.0
    DESCRIPTION "Code of the Geeks VST3 plugin family"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# We only build our own plugins here - skip the SDK's bundled examples/hosts.
# The generic (host-provided) UI is used for the first releases, so VSTGUI is
# not needed yet; leaving it off keeps the Linux build's system dependencies to
# a minimum. Flip SMTG_ENABLE_VSTGUI_SUPPORT to 1 when adding a custom editor.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES 0)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES 0)
set(SMTG_ENABLE_VSTGUI_SUPPORT 0)
set(SMTG_VSTGUI_ROOT "${vst3sdk_SOURCE_DIR}")

# --- Steinberg VST3 SDK ----------------------------------------------------
# Building the SDK subdirectory defines the smtg_* helper functions used below.
add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# --- Shared DSP library ----------------------------------------------------
add_subdirectory(common)

# --- Plugins ---------------------------------------------------------------
add_subdirectory(plugins/hyrax_limiter)
