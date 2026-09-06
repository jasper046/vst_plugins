#pragma once

namespace cotg::dsp {

// Pi as a portable constant. <cmath>'s M_PI is not defined by MSVC unless
// _USE_MATH_DEFINES is set before the include, so we avoid relying on it.
inline constexpr double kPi = 3.14159265358979323846;

} // namespace cotg::dsp
