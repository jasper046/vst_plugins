#pragma once

#include <cmath>

#include "cotg/dsp/Constants.h"

namespace cotg::dsp {

// One 2nd-order (12 dB/oct) state-variable filter section using Andrew Simper's
// topology-preserving transform (TPT), the same difference equations the web
// auto-mastering tool's SvfSection / ReEQ's svf_filter use, so a render here
// lines up with that chain. Shared by the ferro saturator's and slew limiter's
// complementary HF crossovers (low = lp(x), high = x - lp(x)).
//
// For a Butterworth (maximally flat) crossover use q = kButterworthQ.
class Svf
{
public:
    static constexpr double kButterworthQ = 0.7071067811865476; // 1/sqrt(2)

    void setLowPass(double freqHz, double q, double sampleRate)
    {
        setCoeffs(freqHz, q, sampleRate, 0.0, 0.0, 1.0);
    }

    void setHighPass(double freqHz, double q, double sampleRate)
    {
        const double k = 1.0 / q;
        setCoeffs(freqHz, q, sampleRate, 1.0, -k, -1.0);
    }

    double process(double x)
    {
        const double v3 = x - ic2_;
        const double v1 = a1_ * ic1_ + a2_ * v3;
        const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return m0_ * x + m1_ * v1 + m2_ * v2;
    }

    void reset() { ic1_ = ic2_ = 0.0; }

private:
    void setCoeffs(double freqHz, double q, double sampleRate, double m0, double m1, double m2)
    {
        const double g = std::tan(kPi * freqHz / sampleRate);
        const double k = 1.0 / q;
        a1_ = 1.0 / (1.0 + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
        m0_ = m0;
        m1_ = m1;
        m2_ = m2;
    }

    double a1_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    double m0_ = 1.0, m1_ = 0.0, m2_ = 0.0;
    double ic1_ = 0.0, ic2_ = 0.0;
};

} // namespace cotg::dsp
