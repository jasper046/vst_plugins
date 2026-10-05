cmake_minimum_required(VERSION 3.25)

# Hyrax Limiter VST3 plugin. Declares its own project() so the SDK's
# per-target helpers (projectversion.h generation, version strings) resolve
# PROJECT_BINARY_DIR to this plugin's build directory.
project(HyraxLimiter VERSION 1.0.2)

set(target HyraxLimiter)

set(hyrax_sources
    source/HyraxDsp.h
    source/HyraxDsp.cpp
    source/HyraxIds.h
    source/HyraxParams.h
    source/HyraxProcessor.h
    source/HyraxProcessor.cpp
    source/HyraxController.h
    source/HyraxController.cpp
    source/HyraxKnob.h
    source/HyraxKnob.cpp
    source/HyraxMeterView.h
    source/HyraxMeterView.cpp
    source/HyraxEntry.cpp
    source/version.h
)

smtg_add_vst3plugin(${target} PACKAGE_NAME "HyraxLimiter" SOURCES_LIST ${hyrax_sources})

target_compile_features(${target} PUBLIC cxx_std_17)

target_include_directories(${target} PRIVATE source)

# Ensure version files are properly included for Windows builds
target_include_directories(${target} PRIVATE ${PROJECT_BINARY_DIR})

# vstgui_support provides the VSTGUI editor (VST3Editor, controls, the custom
# meter view). Enabled via SMTG_ENABLE_VSTGUI_SUPPORT in the root CMakeLists.
target_link_libraries(${target}
    PRIVATE
        sdk
        vstgui_support
        cotg::dsp
)

# Bundle the editor description into the plugin's Resources folder so the
# VST3Editor can load it by name at runtime.
smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/hyrax_editor.uidesc
        resource/front_panel.png
        resource/gain_reduction_meter.png
        resource/knob_big.png
        resource/knob_small.png
        resource/bypass_button.png
        resource/true_peak_button.png
        resource/on_off_green.png
)

smtg_target_configure_version_file(${target})

if(SMTG_MAC)
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "com.codeofthegeeks.hyraxlimiter"
        COMPANY_NAME "Code of the Geeks")
endif()
