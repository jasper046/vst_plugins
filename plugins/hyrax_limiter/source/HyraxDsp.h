#pragma once

#include "cotg/dsp/Oversampler.h"
#include "cotg/dsp/RingBuffer.h"
#include "cotg/dsp/SoftClipper.h"

namespace cotg::hyrax {

// Sample-rate-independent parameter set, in plain (human) units. Mirrors the
// limiter's controls.
struct Params
{
    double thresholdDb = 0.0;     // -30..0
    double ceilingDb = -0.1;      // -6..0
    double lookAheadMs = 1.0;     // 0..20
    double releaseMs = 3000.0;    // 50..6000
    double stereoLinkPct = 100.0; // 0..100
    bool truePeak = true;         // Off/On
};

// Real-time causal approximation of the Matchering ("Hyrax") mastering limiter.
// Direct port of the JSFX time-domain engine: look-ahead ring buffer -> sliding
// maximum peak detection (optionally true-peak) -> three-stage reduction
// envelope (hard-clip / attack / hold+release) combined by "most reduction
// wins" -> per-channel stereo link -> gain x makeup, followed by an always-on
// 4x-oversampled true-peak safety clipper.
//
// Not thread-safe; drive it from the audio thread only.
class HyraxDsp
{
public:
    // Allocate buffers and derive sample-rate-dependent state. Call before use
    // and whenever the sample rate changes.
    void prepare(double sampleRate);

    // Clear all runtime state (buffers, envelopes) without reallocating.
    void reset();

    // Recompute derived coefficients from the given parameters. Cheap; safe to
    // call every block.
    void setParameters(const Params& p);

    // Process one stereo sample in place.
    void processSample(double& left, double& right);

    // Total latency in samples: look-ahead plus the oversampled safety clipper.
    int latencySamples() const
    {
        return lookSamples_ + cotg::dsp::Oversampler::kLatencySamples;
    }

private:
    double sampleRate_ = 48000.0;

    // --- look-ahead ring buffers (advanced in lockstep) ---
    cotg::dsp::RingBuffer left_;
    cotg::dsp::RingBuffer right_;
    cotg::dsp::RingBuffer peak_;
    int laMax_ = 0;

    // --- derived parameters ---
    double thresh_ = 1.0;    // linear
    double ceiling_ = 1.0;   // linear
    double makeup_ = 1.0;    // ceiling / thresh
    int lookSamples_ = 1;
    int holdSamples_ = 1;
    double holdA_ = 0.0;
    double relA_ = 0.0;
    double atkA_ = 0.0;
    double link_ = 1.0;      // 0..1
    bool truePeak_ = true;

    // --- reduction-domain envelope state (0 = no reduction) ---
    double dAtk_ = 0.0;
    double dHold_ = 0.0;
    double relLp1_ = 0.0;
    double relLp2_ = 0.0;
    int holdCtr_ = 0;

    // --- true-peak interpolation history ---
    double osL1_ = 0.0;
    double osR1_ = 0.0;

    // --- output true-peak safety clipper (4x oversampled, always on) ---
    // Ceiling-tied: the soft knee runs from the Ceiling up to 0 dBFS, so only
    // peaks that escape past the Ceiling are bent and the output can never
    // exceed full scale.
    cotg::dsp::Oversampler osL_;
    cotg::dsp::Oversampler osR_;
    cotg::dsp::SoftClipper softClip_;

    static constexpr double kHoldMs = 1.0;
    static constexpr double kHoldLpHz = 7.0;
    static constexpr double kReleaseLpNum = 800.0;
    static constexpr double kAttackSmooth = 2.0;
};

} // namespace cotg::hyrax
