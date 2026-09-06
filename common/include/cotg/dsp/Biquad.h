#pragma once

namespace cotg::dsp {

// Transposed direct-form II biquad, matching the JSFX update:
//   y   = b0*x + z1;
//   z1  = b1*x - a1*y + z2;
//   z2  = b2*x - a2*y;
// Coefficients are assumed already normalised by a0.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset()
    {
        z1 = 0.0;
        z2 = 0.0;
    }

    double process(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

} // namespace cotg::dsp
