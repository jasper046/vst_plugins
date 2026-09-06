#pragma once

#include <cmath>

#include "cotg/dsp/Constants.h"

namespace cotg::dsp {

// One-pole smoother matching the JSFX idiom  state = x + a*(state - x).
// `a` is the pole coefficient in [0, 1): a = 0 is instantaneous, a -> 1 is
// very slow. Derive it from a cutoff with coeffFromCutoff(), or from a time
// constant (in samples) with coeffFromSamples().
struct OnePole
{
    double a = 0.0;
    double state = 0.0;

    void reset(double value = 0.0) { state = value; }

    double process(double x)
    {
        state = x + a * (state - x);
        return state;
    }

    // First-order (Butterworth) low-pass pole: a = exp(-2*pi*fc/fs).
    static double coeffFromCutoff(double fc, double sampleRate)
    {
        return std::exp(-2.0 * kPi * fc / sampleRate);
    }

    // Smoothing over roughly `n` samples: a = exp(-k / n).
    static double coeffFromSamples(double n, double k = 1.0)
    {
        return std::exp(-k / (n > 0.0 ? n : 1.0));
    }
};

} // namespace cotg::dsp
