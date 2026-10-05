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
    kBypass,

    // OUTPUT STAGE toggles (persisted). Appended after kBypass so the IDs 0..6
    // above are unchanged — existing automation, saved state and the .uidesc
    // control-tags keep working. New persisted params must go here, before
    // kNumAutomatable.
    kFerroSaturation,
    kSoftClipperEnable,
    kSlewLimiter,

    // Count of parameters persisted in processor state. Everything below this
    // line is NOT serialized (see getState/setState), so it must stay put — add
    // non-persisted params after it, never before.
    kNumAutomatable,

    // Read-only meters: per-channel gain reduction in dB, published by the
    // processor to the editor's meter. Not automatable and not saved in state.
    kGainReductionL = kNumAutomatable,
    kGainReductionR,

    kNumParams
};

// Transport full-scale (dB) used to normalize the gain-reduction meter
// parameters, so any realistic reduction fits in 0..1. The editor draws them on
// a smaller fixed visible scale (see kGainReductionMeterDb).
constexpr double kGainReductionMaxDb = 24.0;

// Fixed visible full scale (dB) of the editor's gain-reduction meter. Matches
// the printed 0..-6 dB scale on gain_reduction_meter.png; GR beyond this pegs
// the needle. The transport encoding range (kGainReductionMaxDb) stays wider so
// the processor can still report larger reductions, which the meter just clamps.
constexpr double kGainReductionMeterDb = 6.0;

// Plain-value range for a linear parameter, matching the RangeParameter the
// controller registers. Shared so the processor's normalized<->plain mapping
// stays in lockstep with the controller's display mapping.
struct PRange
{
    double min;
    double max;
    double def;
};

constexpr PRange kThresholdRange {-20.0, 0.0, 0.0};
constexpr PRange kCeilingRange {-6.0, 0.0, -0.1};
constexpr PRange kLookAheadRange {0.0, 20.0, 5.0};
constexpr PRange kReleaseRange {50.0, 3000.0, 3000.0};
constexpr PRange kStereoLinkRange {0.0, 100.0, 100.0};

// Toggle defaults (normalized: 0 = Off, 1 = On). The whole OUTPUT STAGE ships
// enabled; the soft-clip guard on keeps the 0 dBFS guarantee.
constexpr double kTruePeakDefaultNorm = 1.0;         // On
constexpr double kFerroSaturationDefaultNorm = 1.0;  // On
constexpr double kSoftClipperDefaultNorm = 1.0;      // On (keeps the 0 dBFS guard)
constexpr double kSlewLimiterDefaultNorm = 1.0;      // On

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
