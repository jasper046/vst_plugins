#pragma once

#include <array>
#include <cmath>

#include "cotg/dsp/Constants.h"
#include "cotg/dsp/Svf.h"

namespace cotg::dsp {

// Ferro-magnetic (tape) saturation -- the OUTPUT STAGE "warmth" effect. A
// real-time streaming port of the offline reference in the web auto-mastering
// tool (web_utils/.../vinyl_filter.cpp class FerroSaturator; frozen 2026-10-05,
// see auto_spectral_dynamics_core/spikes/ferro_saturation/HANDOVER.md).
//
// An open-knee asymmetric waveshaper: clean below a knee (|x| <= knee passes
// through untouched), then a smooth tanh roll toward a soft ceiling > 1 above it
// (rounds peaks instead of clipping), with a small pre-shaper bias for
// even-harmonic "warmth". Distinct from the soft clipper -- that only bends hard
// at the ceiling and is symmetric (odd-only); ferro adds progressive even + odd
// harmonics across the range. No emphasis / no memory (both measured inaudible
// in the prototype and dropped).
//
// In the Hyrax output stage it runs AFTER the limiter and BEFORE the slew
// limiter + soft-clip 0 dBFS guard, so the guard still owns the final ceiling. It
// works in a CEILING-NORMALIZED domain (a sample sitting at the configured master
// ceiling maps to 1.0 before the curve, then back after), so the fixed
// knee/ceiling track the ceiling slider -- material at the ceiling gets the same
// saturation wherever the ceiling is set.
//
// Per channel: HF split -> [input drive -> 4x min-phase oversample -> asymmetric
// open-knee curve -> decimate -> DC block -> fixed makeup] on the low band, then
// + clean (delay-aligned) high band.
//
// HF BYPASS: only content below `hfBypassHz` enters the curve; the high band is
// split off (2nd-order complementary crossover, high = x - lp(x)) and recombined
// clean. A single full-band memoryless saturator intermodulates everything, so a
// sibilant riding loud low end sprays a resonant cluster of sidebands around the
// "s"; keeping HF out of the nonlinearity removes that at the source (how real
// tape behaves -- HF self-erasure) and keeps the low/mid warmth.
//
// MINIMUM-PHASE OVERSAMPLER: the 4x anti-imaging/anti-aliasing FIR is minimum
// phase, not linear phase. A linear-phase (symmetric) lowpass rings symmetrically
// about every transient, injecting audible PRE-echo on a clean master; the
// min-phase filter with the same magnitude puts that ringing after the transient
// where it is natural. Its group delay is `latencySamples()` base samples; the
// clean high band is delayed to match before recombination, so the whole stage
// adds exactly that latency and is otherwise phase-transparent.
//
// MAKEUP: the offline reference matches whole-file RMS (a signal-dependent gain
// over the recombined signal). The curve is static, so in real time a single
// fixed constant suffices (HANDOVER "C++ port" note): makeup = 1/drive, applied
// to the SATURATED LOW BAND ONLY. That makes sub-knee content exactly unity (the
// toggle is transparent when nothing is saturating) and leaves the clean high
// band genuinely clean -- the only reading consistent with both "fixed constant
// calibrated for in_db" and "HF recombined clean". (The offline whole-file g
// instead scales the recombined low+HF together, so on bass-heavy material it
// also trims the HF a touch; the real-time path keeps the HF untouched. Sub-dB,
// and the soft-clip guard owns the ceiling regardless.)
//
// Not thread-safe; drive it from the audio thread only.
class FerroSaturator
{
public:
    // Frozen production values (see HANDOVER.md): drive +3 dB, knee 0.45,
    // ceiling 1.3, asym 0.11, HF bypass 2 kHz.
    static constexpr double kDriveDbDefault = 3.0;
    static constexpr double kKneeDefault = 0.45;
    static constexpr double kCeilingDefault = 1.3;
    static constexpr double kAsymDefault = 0.11;
    static constexpr double kHfBypassHzDefault = 2000.0;

    // driveDb       input gain into the curve (+3 dB, like a tape INPUT knob)
    // knee          |x| <= knee passes clean          (0.45)
    // ceiling       soft ceiling of the tanh roll > 1 (1.3)
    // asym          pre-shaper bias -> even harmonics (0.11; 0 = odd-only)
    // ceilingDbfs   the master ceiling the normalized domain is referenced to
    // hfBypassHz    saturate below this; pass HF above it clean
    //               (>= Nyquist disables the split -> full-band saturation)
    void configure(double sampleRate, double driveDb, double knee, double ceiling,
                   double asym, double ceilingDbfs, double hfBypassHz)
    {
        srate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        knee_ = knee;
        ceiling_ = ceiling;
        asym_ = asym;

        driveLin_ = std::pow(10.0, driveDb / 20.0);
        makeup_ = 1.0 / driveLin_; // fixed makeup (see class note)
        setCeilingDbfs(ceilingDbfs);

        // Subtract curve(asym) so the quiescent (x=0) output is 0 -- silence stays
        // silent; the DC blocker removes the residual offset the asymmetry leaves.
        offset_ = openCurve(asym_, knee_, ceiling_);
        dcPole_ = std::exp(-2.0 * kPi * kDcBlockHz / srate_);

        split_ = hfBypassHz > 0.0 && hfBypassHz < srate_ * 0.5;
        if (split_)
        {
            lp_[0].setLowPass(hfBypassHz, Svf::kButterworthQ, srate_);
            lp_[1].setLowPass(hfBypassHz, Svf::kButterworthQ, srate_);
        }
        lat_ = osLatency();
        reset();
    }

    // Update only the ceiling reference (the master-ceiling slider the normalized
    // domain tracks) without disturbing the streaming filter state, so it is safe
    // to call every block. drive/knee/ceiling/asym/HF-bypass stay as configured.
    void setCeilingDbfs(double ceilingDbfs)
    {
        const double ceilingLin = std::pow(10.0, ceilingDbfs / 20.0);
        preGain_ = driveLin_ / ceilingLin; // ceiling-normalize, then input drive
        postGain_ = ceilingLin;            // de-normalize after the curve
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            xh_[c].fill(0.0);
            uh_[c].fill(0.0);
            hd_[c].fill(0.0);
            xw_[c] = 0;
            uw_[c] = 0;
            hw_[c] = 0;
            dcX1_[c] = 0.0;
            dcY1_[c] = 0.0;
            lp_[c].reset();
        }
    }

    // Process one stereo sample in place.
    void process(double& left, double& right)
    {
        double* io[2] = {&left, &right};
        for (int c = 0; c < 2; ++c)
        {
            const double s = *io[c];
            double lo = s, high = 0.0;
            if (split_)
            {
                lo = lp_[c].process(s); // saturate the low band only
                high = s - lo;          // clean HF, added back after the curve
            }

            const double wet = osStep(c, lo);  // 4x shaped + decimated, lags lat_
            const double v = wet * postGain_;
            const double yn = v - dcX1_[c] + dcPole_ * dcY1_[c]; // DC block
            dcX1_[c] = v;
            dcY1_[c] = yn;
            const double lowOut = yn * makeup_;

            // Delay the clean high band by the oversampler's group delay so it
            // realigns with the saturated low band at recombination.
            *io[c] = split_ ? lowOut + delayHigh(c, high) : lowOut;
        }
    }

    // Group delay added by the stage, in base-rate samples (the min-phase
    // oversampler's latency). Report this to the host when the stage is engaged.
    int latencySamples() const { return lat_; }

    // Same value as a fixed constant, without a configured instance: the
    // min-phase FIR is defined in normalized frequency, so its group delay is a
    // fixed number of base samples independent of sample rate. Lets the host
    // learn the stage's latency before the DSP is prepared.
    static int filterLatencySamples() { return osLatency(); }

private:
    // DC blocker corner, matching the reference.
    static constexpr double kDcBlockHz = 10.0;

    // 4x polyphase-FIR oversampler geometry (powers of two so index wrap is a
    // mask), matching the reference.
    static constexpr int kOS = 4;
    static constexpr int kPTaps = 16;            // taps per polyphase branch
    static constexpr int kFirLen = kOS * kPTaps; // 64-tap prototype lowpass
    static constexpr int kDelayCap = 32;         // HF-align ring (>> lat_)

    // Open-knee tape curve on v: linear while |v| <= knee (passes clean), then a
    // smooth tanh roll toward `ceiling`. Continuous in value and slope at the knee.
    static double openCurve(double v, double knee, double ceiling)
    {
        const double a = std::fabs(v);
        if (a <= knee)
            return v;
        const double s = ceiling - knee;
        return std::copysign(knee + s * std::tanh((a - knee) / s), v);
    }

    // Minimum-phase 4x lowpass prototype, built once. A Kaiser-windowed sinc
    // (linear phase) converted to its minimum-phase equivalent via the real
    // cepstrum (fold the anticausal part onto the causal part), preserving the
    // magnitude response. Normalized to sum == 1 (DC gain 1); the interpolator
    // multiplies by kOS to restore amplitude after the implicit zero-stuffing.
    static const std::array<double, kFirLen>& osFir()
    {
        static const std::array<double, kFirLen> h = [] {
            // Linear-phase windowed-sinc prototype.
            std::array<double, kFirLen> lin {};
            const double center = (kFirLen - 1) / 2.0;
            const double fc = 0.90 / (2.0 * kOS);
            const double beta = 8.0;
            const double i0b = besselI0(beta);
            for (int n = 0; n < kFirLen; ++n)
            {
                const double m = n - center;
                const double sinc = std::fabs(m) < 1e-9
                                        ? 2.0 * fc
                                        : std::sin(2.0 * kPi * fc * m) / (kPi * m);
                const double r = 2.0 * n / (kFirLen - 1) - 1.0;
                const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
                lin[n] = sinc * w;
            }

            // Real-cepstrum minimum-phase reconstruction (N >> kFirLen).
            constexpr int N = 1024;
            std::array<double, N> re {}, im {};
            for (int n = 0; n < kFirLen; ++n)
                re[n] = lin[n];
            dft(re, im, +1);
            for (int k = 0; k < N; ++k)
            {
                re[k] = std::log(std::hypot(re[k], im[k]) + 1e-7);
                im[k] = 0.0;
            }
            dft(re, im, -1); // -> real cepstrum
            std::array<double, N> cr {}, ci {};
            cr[0] = re[0]; // fold anticausal onto causal
            for (int k = 1; k < N / 2; ++k)
                cr[k] = 2.0 * re[k];
            cr[N / 2] = re[N / 2];
            dft(cr, ci, +1);
            for (int k = 0; k < N; ++k)
            {
                const double ex = std::exp(cr[k]);
                cr[k] = ex * std::cos(ci[k]);
                ci[k] = ex * std::sin(ci[k]);
            }
            dft(cr, ci, -1); // -> min-phase impulse (energy up front)

            std::array<double, kFirLen> c {};
            double sum = 0.0;
            for (int n = 0; n < kFirLen; ++n)
            {
                c[n] = cr[n];
                sum += c[n];
            }
            for (double& v : c)
                v /= sum; // DC gain 1
            return c;
        }();
        return h;
    }

    // Group delay of the up -> identity -> down chain = the peak of its impulse
    // response. Computed once; sample-rate independent (the FIR is defined in
    // normalized frequency), so this is a constant number of base samples.
    static int osLatency()
    {
        static const int lat = [] {
            constexpr int N = 256;
            FerroSaturator probe;
            probe.preGain_ = 1.0;
            probe.postGain_ = 1.0;
            probe.makeup_ = 1.0;
            probe.offset_ = 0.0;
            probe.knee_ = 1e9; // identity curve (never leaves the linear region)
            probe.ceiling_ = 1e9;
            probe.asym_ = 0.0;
            probe.lat_ = 0;
            probe.reset();
            int k = 0;
            double m = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const double y = probe.osStep(0, i == 0 ? 1.0 : 0.0);
                const double a = std::fabs(y);
                if (a > m)
                {
                    m = a;
                    k = i;
                }
            }
            return k;
        }();
        return lat;
    }

    // Modified Bessel I0, for the Kaiser window.
    static double besselI0(double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x * x) / (4.0 * k * k);
            sum += term;
            if (term < 1e-13 * sum)
                break;
        }
        return sum;
    }

    // Naive DFT (N a few hundred, run a handful of times at first use only).
    // sign=+1 forward, sign=-1 inverse (scaled by 1/N).
    template <std::size_t N>
    static void dft(std::array<double, N>& re, std::array<double, N>& im, int sign)
    {
        std::array<double, N> ar = re, ai = im;
        for (std::size_t k = 0; k < N; ++k)
        {
            double sr = 0.0, si = 0.0;
            for (std::size_t nn = 0; nn < N; ++nn)
            {
                const double ang = 2.0 * kPi * (double)((k * nn) % N) / (double)N;
                const double c = std::cos(ang), s = sign * std::sin(ang);
                sr += ar[nn] * c - ai[nn] * s;
                si += ar[nn] * s + ai[nn] * c;
            }
            re[k] = sr;
            im[k] = si;
        }
        if (sign < 0)
            for (std::size_t k = 0; k < N; ++k)
            {
                re[k] /= (double)N;
                im[k] /= (double)N;
            }
    }

    // One base input sample -> one decimated, waveshaped base output sample,
    // 4x oversampled through the min-phase FIR. Streams continuously, so the
    // output lags the input by lat_ base samples (the FIR group delay); no
    // head-drop. Mirrors the reference os_run inner loop with the shape applied
    // at the 4x rate.
    double osStep(int c, double x)
    {
        const auto& h = osFir();
        auto& xh = xh_[c];
        auto& uh = uh_[c];
        int& xw = xw_[c];
        int& uw = uw_[c];

        xw = (xw + 1) & (kPTaps - 1);
        xh[xw] = x;

        double y = 0.0;
        for (int p = 0; p < kOS; ++p)
        {
            double acc = 0.0;
            for (int k = 0; k < kPTaps; ++k)
                acc += h[p + kOS * k] * xh[(xw - k) & (kPTaps - 1)];
            // interpolate (restore amplitude) + shape
            uh[uw] = shape(kOS * acc);
            const int last = uw;
            uw = (uw + 1) & (kFirLen - 1);
            if (p == kOS - 1) // decimator output, once per input frame
            {
                double dacc = 0.0;
                for (int j = 0; j < kFirLen; ++j)
                    dacc += h[j] * uh[(last - j) & (kFirLen - 1)];
                y = dacc;
            }
        }
        return y;
    }

    // Asymmetric open-knee waveshape at the oversampled rate.
    double shape(double v) const
    {
        return openCurve(v * preGain_ + asym_, knee_, ceiling_) - offset_;
    }

    // Push one clean-HF sample, return it delayed by lat_ base samples.
    double delayHigh(int c, double x)
    {
        auto& hd = hd_[c];
        int& hw = hw_[c];
        hd[hw] = x;
        int r = hw - lat_;
        if (r < 0)
            r += kDelayCap;
        const double y = hd[r];
        hw = (hw + 1) % kDelayCap;
        return y;
    }

    // --- configuration ---
    double srate_ = 48000.0;
    double driveLin_ = 1.0;  // linear input drive; makeup = 1/driveLin_
    double preGain_ = 1.0;   // ceiling-normalize * input drive, before the curve
    double postGain_ = 1.0;  // ceiling de-normalize, after the curve
    double makeup_ = 1.0;    // fixed level-match, on the saturated low band only
    double knee_ = kKneeDefault;
    double ceiling_ = kCeilingDefault;
    double asym_ = kAsymDefault;
    double offset_ = 0.0;    // curve(asym), subtracted to keep silence silent
    double dcPole_ = 0.0;
    bool split_ = true;
    int lat_ = 0;

    // --- per-channel streaming state ---
    Svf lp_[2];                             // HF-split low-pass (high = x - lp)
    std::array<double, kPTaps> xh_[2] {};   // oversampler input ring
    std::array<double, kFirLen> uh_[2] {};  // oversampler 4x shaped-sample ring
    int xw_[2] = {0, 0};
    int uw_[2] = {0, 0};
    double dcX1_[2] = {0.0, 0.0};           // DC blocker state
    double dcY1_[2] = {0.0, 0.0};
    std::array<double, kDelayCap> hd_[2] {}; // clean-HF alignment delay
    int hw_[2] = {0, 0};
};

} // namespace cotg::dsp
