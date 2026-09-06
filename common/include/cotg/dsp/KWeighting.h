#pragma once

#include <cmath>

#include "cotg/dsp/Biquad.h"

namespace cotg::dsp {

// ITU-R BS.1770 K-weighting: a high-shelf "pre-filter" (stage 1) followed by a
// high-pass (stage 2). The reference coefficients are specified at 48 kHz; the
// analogue prototypes are re-derived here for the actual sample rate so the
// weighting is correct at any rate. Ported verbatim from the JSFX
// kweight_coeffs() in hyrax_limiter.jsfx.
struct KWeighting
{
    Biquad shelf;
    Biquad highpass;

    // Builds fresh biquads for the given sample rate. Filter state is reset.
    void configure(double sampleRate)
    {
        // --- stage 1: high-shelf (+~4 dB above ~1681 Hz) ---
        const double dbBoost = 3.999843853973347;
        const double fcShelf = 1681.974450955533;
        const double qShelf  = 0.7071752369554196;
        const double K  = std::tan(M_PI * fcShelf / sampleRate);
        const double Vh = std::pow(10.0, dbBoost / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0s = 1.0 + K / qShelf + K * K;

        shelf.b0 = (Vh + Vb * K / qShelf + K * K) / a0s;
        shelf.b1 = 2.0 * (K * K - Vh) / a0s;
        shelf.b2 = (Vh - Vb * K / qShelf + K * K) / a0s;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0s;
        shelf.a2 = (1.0 - K / qShelf + K * K) / a0s;
        shelf.reset();

        // --- stage 2: high-pass (~38 Hz) ---
        const double fcHp = 38.13547087602444;
        const double qHp  = 0.5003270373238773;
        const double w0 = 2.0 * M_PI * fcHp / sampleRate;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * qHp);
        const double a0h = 1.0 + alpha;

        highpass.b0 = (1.0 + cw) / 2.0 / a0h;
        highpass.b1 = -(1.0 + cw) / a0h;
        highpass.b2 = (1.0 + cw) / 2.0 / a0h;
        highpass.a1 = -2.0 * cw / a0h;
        highpass.a2 = (1.0 - alpha) / a0h;
        highpass.reset();
    }

    void reset()
    {
        shelf.reset();
        highpass.reset();
    }

    // K-weight one sample (shelf then high-pass).
    double process(double x)
    {
        return highpass.process(shelf.process(x));
    }
};

} // namespace cotg::dsp
