#pragma once

#include <array>
#include <cmath>

#include "cotg/dsp/Svf.h"

namespace cotg::dsp {

// Flat (no-RIAA) per-channel content-adaptive slew limiter -- the digital-master
// "HF smoothness" OUTPUT STAGE effect. A real-time streaming port of the offline
// flat_slew in web_utils/.../vinyl_filter.cpp (shipped defaults pct 99.9, knee
// 0.3, hp 3 kHz).
//
// For each stereo channel independently: split off the high band
// (high = x - lp(x), 2nd-order Butterworth at hpHz), cap the per-sample slew of
// the high band at Smax with a quadratic soft knee reaching kneeFrac*Smax below
// it (recursive on the OUTPUT so it is the output slew that is bounded), then
// recombine low + limited high. Only the sharpest ~(100-pct)% of transitions are
// clamped, so it de-stresses HF harshness without the distortion a fixed
// threshold on a band-passed high band produces. With hpHz <= 0 it limits the
// full band.
//
// ONLINE Smax -- the one piece that is not naturally streaming. The offline tool
// sets Smax to the pct-th percentile of the WHOLE FILE's |per-sample slew| (a
// global statistic). Here that becomes a NON-DECAYING log-histogram of the high
// band's |slew|, queried periodically: for the quasi-stationary material a master
// is, it converges to the same whole-file percentile the offline tool uses (so a
// plugin render matches the offline tool in steady state). Smax ramps from loose
// (no limiting) to the steady value over the first fraction of a second as the
// histogram fills -- benign, since under-limiting is safe and the downstream
// soft-clip guard still owns 0 dBFS.
//
// The IIR split and the recursive cap add NO latency.
//
// Not thread-safe; drive it from the audio thread only.
class SlewLimiter
{
public:
    static constexpr double kPctDefault = 99.9;      // percentile of |slew| -> Smax
    static constexpr double kKneeFracDefault = 0.3;  // soft-knee width, fraction of Smax
    static constexpr double kHpHzDefault = 3000.0;   // split; limit only above this

    void configure(double sampleRate, double pct, double kneeFrac, double hpHz)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        pct_ = pct > 0.0 ? pct : kPctDefault;
        kneeFrac_ = kneeFrac > 0.0 ? kneeFrac : kKneeFracDefault;
        split_ = hpHz > 0.0 && hpHz < sr * 0.5;
        if (split_)
        {
            lp_[0].setLowPass(hpHz, Svf::kButterworthQ, sr);
            lp_[1].setLowPass(hpHz, Svf::kButterworthQ, sr);
        }
        reset();
    }

    void reset()
    {
        updateCtr_ = 0;
        for (int c = 0; c < 2; ++c)
        {
            hist_[c].fill(0.0);
            total_[c] = 0.0;
            smax_[c] = kNoLimit; // loose until the histogram warms up
            snapped_[c] = false;
            prev_[c] = 0.0;
            prevIn_[c] = 0.0;
            lp_[c].reset();
        }
        primed_ = false;
    }

    // Process one stereo sample in place.
    void process(double& left, double& right)
    {
        double* io[2] = {&left, &right};

        // Prime per-channel state on the first sample so the limiter starts from
        // the signal (matches the offline prev = band[0]; no limiting at n=0).
        if (!primed_)
        {
            for (int c = 0; c < 2; ++c)
            {
                const double hi = split_ ? *io[c] - lp_[c].process(*io[c]) : *io[c];
                prev_[c] = hi;
                prevIn_[c] = hi;
            }
            primed_ = true;
            return;
        }

        const bool doUpdate = (++updateCtr_ >= kUpdateInterval);
        if (doUpdate)
            updateCtr_ = 0;

        for (int c = 0; c < 2; ++c)
        {
            const double s = *io[c];
            const double lo = split_ ? lp_[c].process(s) : 0.0;
            const double hi = split_ ? s - lo : s;

            // Feed the running percentile estimator with the input slew.
            addSlew(c, std::fabs(hi - prevIn_[c]));
            prevIn_[c] = hi;
            if (doUpdate)
                refreshSmax(c);

            *io[c] = lo + slewLimit(c, hi);
        }
    }

    int latencySamples() const { return 0; }

private:
    // Log-histogram geometry for |slew| in [kMinSlew, kMaxSlew]. 1024 bins gives
    // ~1.4% (0.12 dB) resolution on the percentile estimate.
    static constexpr int kBins = 1024;
    static constexpr double kMinSlew = 1e-6;
    static constexpr double kMaxSlew = 4.0; // |x[n]-x[n-1]| worst case ~2; headroom
    static constexpr double kNoLimit = 1e9;
    static constexpr double kMinSamples = 64.0;  // warm-up before snapping Smax
    static constexpr int kUpdateInterval = 256;  // samples between percentile queries
    static constexpr double kSmaxGlide = 0.3;    // one-pole glide toward the new target

    static double logRatio() { return std::log(kMaxSlew / kMinSlew); }

    int binOf(double s) const
    {
        if (s <= kMinSlew)
            return 0;
        int i = static_cast<int>((std::log(s / kMinSlew) / logRatio()) * kBins);
        if (i < 0)
            i = 0;
        if (i >= kBins)
            i = kBins - 1;
        return i;
    }

    double binValue(int i) const
    {
        return kMinSlew * std::exp(logRatio() * ((i + 0.5) / kBins));
    }

    void addSlew(int c, double s)
    {
        hist_[c][binOf(s)] += 1.0;
        total_[c] += 1.0;
    }

    // pct-th percentile of the accumulated |slew| distribution: walk down from the
    // top bin until (100-pct)% of the mass is above, that bin's value is Smax.
    double queryPercentile(int c) const
    {
        const double total = total_[c];
        if (total < kMinSamples)
            return kNoLimit;
        const double tail = (1.0 - pct_ / 100.0) * total;
        double acc = 0.0;
        for (int i = kBins - 1; i >= 0; --i)
        {
            acc += hist_[c][i];
            if (acc >= tail)
                return binValue(i);
        }
        return binValue(0);
    }

    void refreshSmax(int c)
    {
        const double target = queryPercentile(c);
        if (target >= kNoLimit)
            return; // still warming up
        if (!snapped_[c])
        {
            smax_[c] = target;
            snapped_[c] = true;
        }
        else
        {
            smax_[c] += kSmaxGlide * (target - smax_[c]);
        }
    }

    // Soft-knee recursive slew cap on one high-band sample. Caps the output slew
    // |q[n]-q[n-1]| at Smax with a quadratic knee reaching kneeFrac*Smax below it.
    // Verbatim port of the reference flat_slew_limit, made per-sample streaming
    // (recursive on prev_ = the previous output).
    double slewLimit(int c, double x)
    {
        const double smax = smax_[c];
        if (smax <= 0.0 || smax >= kNoLimit)
        {
            prev_[c] = x;
            return x;
        }
        const double knee = kneeFrac_ * smax;
        const double thr = smax - knee;
        double d = x - prev_[c];
        const double ad = std::fabs(d);
        if (ad > thr)
        {
            const double over = ad - thr;
            const double mag =
                over >= 2.0 * knee ? smax : thr + over - over * over / (4.0 * knee);
            d = d > 0.0 ? mag : -mag;
        }
        prev_[c] += d;
        return prev_[c];
    }

    // --- config ---
    double pct_ = kPctDefault;
    double kneeFrac_ = kKneeFracDefault;
    bool split_ = true;

    // --- per-channel state ---
    Svf lp_[2];                             // HF split low-pass (high = x - lp)
    std::array<double, kBins> hist_[2] {};  // non-decaying |slew| histogram
    double total_[2] = {0.0, 0.0};
    double smax_[2] = {kNoLimit, kNoLimit};
    bool snapped_[2] = {false, false};
    double prev_[2] = {0.0, 0.0};           // last limiter output (recursive state)
    double prevIn_[2] = {0.0, 0.0};         // last input high-band sample (for slew)
    int updateCtr_ = 0;
    bool primed_ = false;
};

} // namespace cotg::dsp
