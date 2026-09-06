#pragma once

#include <array>
#include <cmath>

#include "cotg/dsp/Constants.h"

namespace cotg::dsp {

// 4x linear-phase polyphase FIR oversampler for a single channel. Splits into an
// upsample step and a downsample step so the caller can apply a non-linear stage
// (e.g. a clipper) in the 4x domain in between:
//
//   double up[4];
//   os.upsample(x, up);
//   for (double& s : up) s = clip(s);
//   double y = os.downsample(up);
//
// The prototype is a Blackman-Harris-windowed sinc low-pass at the base-rate
// Nyquist, computed at construction (no external tables) and normalised to unity
// pass-band gain, so the chain is sample-rate independent and, apart from the
// clip, delays the signal by exactly kLatencySamples base-rate samples.
class Oversampler
{
public:
    static constexpr int kOS = 4;
    static constexpr int kTaps = 97;                        // prototype length (odd)
    static constexpr int kLatencySamples = (kTaps - 1) / kOS; // 24 base samples

    Oversampler()
    {
        buildPrototype();
        reset();
    }

    void reset()
    {
        inHist_.fill(0.0);
        x4Hist_.fill(0.0);
        x4Pos_ = 0;
    }

    // One base-rate input sample -> kOS upsampled sub-samples.
    void upsample(double x, double out[kOS])
    {
        // Newest base sample at index 0.
        for (int k = kInTaps - 1; k > 0; --k)
            inHist_[k] = inHist_[k - 1];
        inHist_[0] = x;

        for (int p = 0; p < kOS; ++p)
        {
            double acc = 0.0;
            for (int k = 0; k < kInTaps; ++k)
            {
                const int idx = kOS * k + p;
                if (idx < kTaps)
                    acc += h_[idx] * inHist_[k];
            }
            out[p] = static_cast<double>(kOS) * acc;
        }
    }

    // kOS (processed) sub-samples -> one base-rate output sample.
    double downsample(const double in[kOS])
    {
        for (int p = 0; p < kOS; ++p)
        {
            x4Hist_[x4Pos_] = in[p];
            if (++x4Pos_ >= kTaps)
                x4Pos_ = 0;
        }

        // Sample the decimation filter on the grid position v[4m] (the first of
        // the four sub-samples just pushed), i.e. h[j] * v[4m - j]. Using the
        // newest sub-sample instead would add a 3/4-sample fractional delay.
        double acc = 0.0;
        int idx = x4Pos_ - kOS;
        for (int j = 0; j < kTaps; ++j)
        {
            if (idx < 0)
                idx += kTaps;
            acc += h_[j] * x4Hist_[idx];
            --idx;
        }
        return acc;
    }

    int latencySamples() const { return kLatencySamples; }

private:
    static constexpr int kInTaps = (kTaps + kOS - 1) / kOS; // taps per polyphase branch

    static double sinc(double t)
    {
        if (std::fabs(t) < 1e-9)
            return 1.0;
        const double x = kPi * t;
        return std::sin(x) / x;
    }

    void buildPrototype()
    {
        const int last = kTaps - 1;
        const double fc = 0.5 / static_cast<double>(kOS); // cutoff at base-rate Nyquist
        double sum = 0.0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double m = n - last / 2.0;
            const double ideal = 2.0 * fc * sinc(2.0 * fc * m);
            // Blackman-Harris window (~ -92 dB side lobes).
            const double w = 0.35875 - 0.48829 * std::cos(2.0 * kPi * n / last) +
                             0.14128 * std::cos(4.0 * kPi * n / last) -
                             0.01168 * std::cos(6.0 * kPi * n / last);
            h_[n] = ideal * w;
            sum += h_[n];
        }
        for (int n = 0; n < kTaps; ++n)
            h_[n] /= sum; // unity pass-band gain
    }

    std::array<double, kTaps> h_ {};
    std::array<double, kInTaps> inHist_ {};
    std::array<double, kTaps> x4Hist_ {};
    int x4Pos_ = 0;
};

} // namespace cotg::dsp
