# vst_plugins

Cross-platform VST3 versions of the [Code of the Geeks REAPER JSFX
effects](../reaper_js_effects), so they can be used in any DAW on Windows,
macOS and Linux.

This repo is a **family** of plugins built on the [Steinberg VST3
SDK](https://github.com/steinbergmedia/vst3sdk). Shared DSP lives in a common
header-only library so each plugin reuses the same primitives.

## Plugins

| Plugin | Status | Source JSFX |
|--------|--------|-------------|
| **Hyrax Limiter** | ✅ v1 (full parity) | `hyrax_limiter.jsfx` |

The Hyrax Limiter is a real-time causal port of the Matchering ("Hyrax")
mastering limiter: look-ahead peak limiting with a multi-stage release,
optional true-peak detection, a BS.1770 short-term LUFS meter, and a **SENSE**
loop that auto-rides the threshold toward a target loudness.

It also has an always-on **safety clipper** as its final stage: a 4×-oversampled,
ceiling-tied soft clip (the knee runs from the Ceiling up to 0 dBFS) followed by a
base-rate hard clamp, so the output never actually clips (no sample exceeds
0 dBFS) — replacing the manual brick-wall limiter you'd otherwise chain after it.
It adds a small fixed latency (reported to the host). Peaks at or below the
Ceiling pass untouched; only peaks that escape past it are bent. Note it is a soft
clipper, not a true-peak limiter, so it strongly reduces but does not fully
guarantee inter-sample peaks below 0 dBTP.

## Repository layout

```
common/                     Shared, header-only DSP library (cotg::dsp)
  include/cotg/dsp/         OnePole, RingBuffer, Biquad, KWeighting, LufsMeter
plugins/
  hyrax_limiter/            Hyrax Limiter VST3 (engine + VST3 glue)
external/vst3sdk/           Steinberg VST3 SDK (git submodule)
.github/workflows/build.yml CI: builds Win/mac/Linux, publishes releases
```

Each plugin keeps a pure DSP engine (e.g. `HyraxDsp`) separate from the VST3
processor/controller so the audio maths is testable and stays faithful to the
original JSFX.

## Getting the binaries (no build needed)

Pre-built `.vst3` bundles for Windows, macOS and Linux are attached to each
[GitHub Release](../../releases) and to every CI run under **Actions →
Artifacts**.

Install by copying `HyraxLimiter.vst3` into your system VST3 folder:

- **Windows:** `C:\Program Files\Common Files\VST3\`
- **macOS:** `~/Library/Audio/Plug-Ins/VST3/`
- **Linux:** `~/.vst3/`

## Building from source

Requires CMake ≥ 3.25 and a C++17 compiler (MSVC 2019+, Xcode 10+, or GCC 13+).

```sh
git clone --recursive https://github.com/jasper046/vst_plugins.git
cd vst_plugins
# if you forgot --recursive:
git submodule update --init --recursive

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

On Linux the build also symlinks the plugin into `~/.vst3/`, so it is
immediately available to a DAW (e.g. REAPER) for testing.

### Linux system packages

The SDK needs a handful of dev packages (Debian/Ubuntu names):

```sh
sudo apt-get install -y libx11-dev libxext-dev libxcb1-dev libxcb-util-dev \
  libxcb-cursor-dev libxcb-keysyms1-dev libxcb-xkb-dev libxkbcommon-dev \
  libxkbcommon-x11-dev libfontconfig1-dev libcairo2-dev libgtkmm-3.0-dev \
  libsqlite3-dev libfreetype6-dev
```

## Validating

The SDK builds a `validator` that runs Steinberg's conformance tests:

```sh
./build/bin/validator build/VST3/Release/HyraxLimiter.vst3
```

## Licensing

This project is MIT licensed (see [LICENSE](LICENSE)). The Steinberg VST3 SDK
in `external/vst3sdk` is MIT © Steinberg Media Technologies. "VST" is a
trademark of Steinberg Media Technologies GmbH.
