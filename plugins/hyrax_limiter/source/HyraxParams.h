#pragma once

#include <algorithm>

#include "pluginterfaces/vst/vsttypes.h"

namespace cotg::hyrax {

// Parameter identifiers (all automatable controls).
enum ParamId : Steinberg::Vst::ParamID
{
    kThreshold = 0,
    kCeiling,
    kLookAhead,
    kRelease,
    kStereoLink,
    kTruePeak,

    kNumAutomatable,
    kNumParams = kNumAutomatable
};

// Plain-value range for a linear parameter, matching the RangeParameter the
// controller registers. Shared so the processor's normalized<->plain mapping
// stays in lockstep with the controller's display mapping.
struct PRange
{
    double min;
    double max;
    double def;
};

constexpr PRange kThresholdRange {-30.0, 0.0, 0.0};
constexpr PRange kCeilingRange {-6.0, 0.0, -0.1};
constexpr PRange kLookAheadRange {0.0, 20.0, 1.0};
constexpr PRange kReleaseRange {50.0, 6000.0, 3000.0};
constexpr PRange kStereoLinkRange {0.0, 100.0, 100.0};

// Toggle default (normalized: 0 = Off, 1 = On).
constexpr double kTruePeakDefaultNorm = 1.0; // On

inline double toPlain(const PRange& r, double norm)
{
    return r.min + norm * (r.max - r.min);
}

inline double toNorm(const PRange& r, double plain)
{
    const double n = (plain - r.min) / (r.max - r.min);
    return std::clamp(n, 0.0, 1.0);
}

} // namespace cotg::hyrax
