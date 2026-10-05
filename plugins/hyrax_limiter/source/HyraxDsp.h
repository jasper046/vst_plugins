#pragma once

#include "cotg/dsp/FerroSaturator.h"
#include "cotg/dsp/Oversampler.h"
#include "cotg/dsp/RingBuffer.h"
#include "cotg/dsp/SlewLimiter.h"
#include "cotg/dsp/SoftClipper.h"

namespace cotg::hyrax {

// Sample-rate-independent parameter set, in plain (human) units. Mirrors the
// limiter's controls.
struct Params
{
    double thresholdDb = 0.0;     // -20..0
    double ceilingDb = -0.1;      // -6..0
    double lookAheadMs = 5.0;     // 0..20
    double releaseMs = 3000.0;    // 50..3000
    double stereoLinkPct = 100.0; // 0..100
    bool truePeak = true;         // Off/On

    // OUTPUT STAGE toggles.
    bool ferro = false;    // ferro-magnetic saturation
    bool softClip = true;  // 0 dBFS safety guard (On preserves the guarantee)
    bool slew = false;     // flat slew-rate limiter (HF smoothness)
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
    // and whenever the sample rate changes. maxLookAheadMs is the top of the
    // Look-Ahead parameter range: the engine always delays the output by this
    // much (a trailing delay makes up the difference below the current setting),
    // so reported latency is constant and moving Look-Ahead never renegotiates
    // the host's delay compensation.
    void prepare(double sampleRate, double maxLookAheadMs);

    // Clear all runtime state (buffers, envelopes) without reallocating.
    void reset();

    // Recompute derived coefficients from the given parameters. Cheap; safe to
    // call every block.
    void setParameters(const Params& p);

    // Process one stereo sample in place.
    void processSample(double& left, double& right);

    // Total latency in samples: the MAXIMUM look-ahead, the always-on oversampled
    // safety clipper, and the ferro stage. Everything is counted unconditionally
    // -- Look-Ahead below its max is made up by a trailing delay, ferro-off runs
    // through a matched delay -- so no parameter changes latency during playback.
    // That avoids the mid-playback PDC renegotiation some hosts only re-sync on
    // transport restart (which left a lasting delay mismatch).
    int latencySamples() const
    {
        return maxLookSamples_ + cotg::dsp::Oversampler::kLatencySamples +
               ferro_.latencySamples();
    }

    // Per-channel gain reduction applied to the most recently processed sample,
    // in positive dB (0 = no reduction). Reflects the limiter envelope; the
    // always-on safety clipper is not included. Drives the editor's L/R meters.
    double gainReductionDbL() const { return grDbL_; }
    double gainReductionDbR() const { return grDbR_; }

private:
    double sampleRate_ = 48000.0;

    // --- look-ahead ring buffers (advanced in lockstep) ---
    cotg::dsp::RingBuffer left_;
    cotg::dsp::RingBuffer right_;
    cotg::dsp::RingBuffer peak_;
    int laMax_ = 0;
    int maxLookSamples_ = 0; // = max Look-Ahead; the constant output delay target

    // Trailing delay that pads the current look-ahead up to maxLookSamples_, so
    // total latency stays constant as Look-Ahead changes. Runs last, after the
    // whole output stage. Fed every sample so moving Look-Ahead only steps the
    // read offset (one-time discontinuity, no latency change).
    cotg::dsp::RingBuffer laCompL_;
    cotg::dsp::RingBuffer laCompR_;

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

    // --- metering: per-channel gain reduction of the last sample, positive dB ---
    double grDbL_ = 0.0;
    double grDbR_ = 0.0;

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

    // --- OUTPUT STAGE: ferro -> slew -> soft-clip 0 dBFS guard (soft clip last
    // so it owns the final ceiling). Ferro and slew are toggled; the soft clip
    // defaults on. ---
    cotg::dsp::FerroSaturator ferro_;
    cotg::dsp::SlewLimiter slew_;
    bool ferroEnabled_ = false;
    bool softClipEnabled_ = true;
    bool slewEnabled_ = false;

    // Soft clipper acts as a transparent 0 dBFS guard: ceiling 0 dBFS, a 0.5 dB
    // knee below it. Independent of the Ceiling slider (the limiter enforces
    // that), so normal program is untouched and only true 0 dBFS overs are bent.
    static constexpr double kSoftClipGuardKneeDb = 0.5;

    // Pure integer delays matching the oversampler's group delay, feeding the
    // guard-OFF path. The oversampled up/down round-trip is a (mild) low-pass,
    // not a perfect delay, so leaving it in the signal path when the guard is
    // disabled would colour the output (an audible HF residual in out - in).
    // Routing around it through a matched delay keeps "guard off" transparent
    // while holding total latency constant (no host restart on the toggle).
    cotg::dsp::RingBuffer scBypassL_;
    cotg::dsp::RingBuffer scBypassR_;

    // Matched delay (= ferro's latency) for the ferro-OFF path, so ferro's
    // latency is always present and the toggle never changes reported latency.
    cotg::dsp::RingBuffer ferroBypassL_;
    cotg::dsp::RingBuffer ferroBypassR_;

    static constexpr double kHoldMs = 1.0;
    static constexpr double kHoldLpHz = 7.0;
    static constexpr double kReleaseLpNum = 800.0;
    static constexpr double kAttackSmooth = 2.0;
};

} // namespace cotg::hyrax
