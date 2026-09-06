#pragma once

#include <cmath>

namespace cotg::dsp {

// dB-domain quadratic soft-knee clipper. Memoryless. Ported from Schwa's
// soft_clipper JSFX (via the webutils auto-mastering vinyl_filter.cpp): above a
// threshold `knee` dB below the ceiling, the overshoot is bent by a quadratic
// that leaves the threshold at unity slope and reaches the ceiling with zero
// slope, so the curve is continuous in level and in slope at both ends. Past an
// overshoot of 2*knee the level is held at the ceiling. With ceilingDb = 0 the
// output magnitude can never exceed unity (0 dBFS).
//
// Ceiling-tied use in the limiter: ceilingDb = 0, kneeDb = -limiterCeilingDb, so
// the knee runs from the limiter ceiling up to full scale and only peaks that
// escape past the ceiling are bent.
class SoftClipper
{
public:
    void configure(double ceilingDb, double kneeDb)
    {
        ceilingDb_ = ceilingDb;
        kneeDb_ = kneeDb > 0.0 ? kneeDb : 0.0;
        thresholdDb_ = ceilingDb_ - kneeDb_;
    }

    double clip(double x) const
    {
        const double mag = std::fabs(x);
        if (mag < 1e-12)
            return x;

        double db = kAmpDb * std::log(mag);
        if (db > thresholdDb_)
        {
            if (kneeDb_ <= 1e-6)
            {
                // Degenerate knee -> hard clip at the ceiling.
                db = ceilingDb_;
            }
            else
            {
                const double over = db - thresholdDb_;
                db = over >= 2.0 * kneeDb_
                         ? ceilingDb_
                         : thresholdDb_ + over - (over * over) / (4.0 * kneeDb_);
            }
        }
        return std::copysign(std::exp(db / kAmpDb), x);
    }

private:
    // Natural-log magnitude -> dB, matching the JSFX this is ported from.
    static constexpr double kAmpDb = 8.6562;

    double ceilingDb_ = 0.0;
    double kneeDb_ = 0.0;
    double thresholdDb_ = 0.0;
};

} // namespace cotg::dsp
