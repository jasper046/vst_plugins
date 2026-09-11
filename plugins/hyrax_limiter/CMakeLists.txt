cmake_minimum_required(VERSION 3.25)

# Hyrax Limiter VST3 plugin. Declares its own project() so the SDK's
# per-target helpers (projectversion.h generation, version strings) resolve
# PROJECT_BINARY_DIR to this plugin's build directory.
project(HyraxLimiter VERSION 0.1.1)

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
    source/HyraxEntry.cpp
    source/version.h
)

smtg_add_vst3plugin(${target} PACKAGE_NAME "HyraxLimiter" SOURCES_LIST ${hyrax_sources})

target_compile_features(${target} PUBLIC cxx_std_17)

target_include_directories(${target} PRIVATE source)

# Ensure version files are properly included for Windows builds
target_include_directories(${target} PRIVATE ${PROJECT_BINARY_DIR})

target_link_libraries(${target}
    PRIVATE
        sdk
        cotg::dsp
)

smtg_target_configure_version_file(${target})

if(SMTG_MAC)
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "com.codeofthegeeks.hyraxlimiter"
        COMPANY_NAME "Code of the Geeks")
endif()
