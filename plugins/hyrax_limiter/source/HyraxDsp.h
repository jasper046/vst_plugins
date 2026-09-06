#pragma once

#include "cotg/dsp/LufsMeter.h"
#include "cotg/dsp/Oversampler.h"
#include "cotg/dsp/RingBuffer.h"
#include "cotg/dsp/SoftClipper.h"

namespace cotg::hyrax {

// Sample-rate-independent parameter set, in plain (human) units. Mirrors the
// eight JSFX sliders of hyrax_limiter.jsfx.
struct Params
{
    double thresholdDb = 0.0;    // slider1  -30..0
    double ceilingDb = -0.1;     // slider2  -6..0
    double lookAheadMs = 1.0;    // slider3  0..20
    double releaseMs = 3000.0;   // slider4  50..6000
    double stereoLinkPct = 100.0;// slider5  0..100
    bool truePeak = true;        // slider6  Off/On
    double targetLufs = -12.0;   // slider7  -30..-5
    bool senseOn = false;        // slider8  Off/On
};

// Real-time causal approximation of the Matchering ("Hyrax") mastering limiter.
// Direct port of the JSFX time-domain engine: look-ahead ring buffer -> sliding
// maximum peak detection (optionally true-peak) -> three-stage reduction
// envelope (hard-clip / attack / hold+release) combined by "most reduction
// wins" -> per-channel stereo link -> gain x makeup. Also runs a BS.1770
// short-term LUFS meter on the output and an optional SENSE loop that rides the
// threshold toward a target loudness.
//
// Not thread-safe; drive it from the audio thread only.
class HyraxDsp
{
public:
    // Allocate buffers and derive sample-rate-dependent state. Call before use
    // and whenever the sample rate changes.
    void prepare(double sampleRate);

    // Clear all runtime state (buffers, envelopes, meters) without reallocating.
    void reset();

    // Recompute derived coefficients from the given parameters (the @slider
    // block). Cheap; safe to call every block. The threshold is also taken as
    // the current working threshold, which the SENSE loop may subsequently move.
    void setParameters(const Params& p);

    // Process one stereo sample in place.
    void processSample(double& left, double& right);

    // Total latency in samples: look-ahead plus the always-on oversampled
    // safety clipper.
    int latencySamples() const
    {
        return lookSamples_ + cotg::dsp::Oversampler::kLatencySamples;
    }

    // Smoothed gain reduction, in dB (<= 0). 0 dB means no reduction.
    double gainReductionDb() const;

    // Current BS.1770 short-term (3 s) LUFS of the output.
    double shortTermLufs() const { return lufs_.shortTerm(); }

    // SENSE offset currently applied to the threshold (dB, <= 0). The offset
    // lives entirely inside the engine and is never written back to the host,
    // so host parameter echo/re-send cannot reset the loop.
    double senseOffsetDb() const { return senseOffsetDb_; }

    // Effective threshold actually used = user threshold + SENSE offset (dB).
    double effectiveThresholdDb() const;

private:
    void updateThreshold();

    double sampleRate_ = 48000.0;

    // --- look-ahead ring buffers (advanced in lockstep) ---
    cotg::dsp::RingBuffer left_;
    cotg::dsp::RingBuffer right_;
    cotg::dsp::RingBuffer peak_;
    int laMax_ = 0;

    // --- derived parameters (@slider) ---
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

    // --- gain-reduction meter ---
    double grMeter_ = 1.0;
    double grDecay_ = 1.0;

    // --- LUFS metering ---
    cotg::dsp::LufsMeter lufs_;

    // --- output true-peak safety clipper (4x oversampled, always on) ---
    // Ceiling-tied: the soft knee runs from the limiter Ceiling up to 0 dBFS, so
    // only peaks that escape past the Ceiling are bent and the output can never
    // exceed full scale.
    cotg::dsp::Oversampler osL_;
    cotg::dsp::Oversampler osR_;
    cotg::dsp::SoftClipper softClip_;

    // --- SENSE loop ---
    // The user's Threshold parameter and the SENSE-applied offset are kept
    // separate; the effective threshold is their (clamped) sum. Because the
    // offset never leaves the engine, the host Threshold parameter is never
    // written to and host parameter echo cannot reset the loop.
    bool senseOn_ = false;
    double targetLufs_ = -14.0;
    double userThresholdDb_ = 0.0;
    double senseOffsetDb_ = 0.0;
    int senseUpdateEvery_ = 1;
    int senseUpdateCtr_ = 0;

    static constexpr double kThresholdMinDb = -30.0;
    static constexpr double kThresholdMaxDb = 0.0;
    static constexpr double kSenseGain = 0.03;    // proportional step fraction
    static constexpr double kSenseRateDb = 0.5;   // max threshold move per second
    static constexpr double kHoldMs = 1.0;
    static constexpr double kHoldLpHz = 7.0;
    static constexpr double kReleaseLpNum = 800.0;
    static constexpr double kAttackSmooth = 2.0;
};

} // namespace cotg::hyrax
