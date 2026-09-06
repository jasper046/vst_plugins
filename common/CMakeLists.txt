# Shared DSP library for the plugin family. Header-only, no VST dependencies,
# so it can be reused by every plugin and by offline tests.
add_library(cotg_dsp INTERFACE)
add_library(cotg::dsp ALIAS cotg_dsp)

target_include_directories(cotg_dsp INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_features(cotg_dsp INTERFACE cxx_std_17)
