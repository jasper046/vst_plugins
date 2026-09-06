#pragma once

#include <cmath>
#include <vector>

#include "cotg/dsp/KWeighting.h"

namespace cotg::dsp {

// ITU-R BS.1770 short-term (3 s) LUFS meter for a stereo signal. Each channel
// runs through its own K-weighting filter; the per-channel mean-squares are
// summed (channel weight 1.0 for L/R) over a sliding window and converted to
// LUFS. Ported from the LUFS section of hyrax_limiter.jsfx.
class LufsMeter
{
public:
    // windowSeconds defaults to the BS.1770 short-term integration time.
    void configure(double sampleRate, double windowSeconds = 3.0)
    {
        kL_.configure(sampleRate);
        kR_.configure(sampleRate);

        const int len = static_cast<int>(std::ceil(windowSeconds * sampleRate));
        msBuf_.assign(len > 0 ? len : 1, 0.0);
        msPos_ = 0;
        msSum_ = 0.0;
        lufs_ = kSilence;
    }

    void reset()
    {
        kL_.reset();
        kR_.reset();
        std::fill(msBuf_.begin(), msBuf_.end(), 0.0);
        msPos_ = 0;
        msSum_ = 0.0;
        lufs_ = kSilence;
    }

    // Feed one stereo output sample. Returns nothing; read shortTerm() when a
    // fresh value is wanted (it is kept continuously up to date).
    void process(double left, double right)
    {
        const double kl = kL_.process(left);
        const double kr = kR_.process(right);
        const double ksq = kl * kl + kr * kr;

        msSum_ += ksq - msBuf_[msPos_];
        msBuf_[msPos_] = ksq;
        if (++msPos_ >= static_cast<int>(msBuf_.size()))
            msPos_ = 0;

        const double meanMs = msSum_ / static_cast<double>(msBuf_.size());
        lufs_ = meanMs > 0.0 ? -0.691 + 10.0 * std::log10(meanMs) : kSilence;
    }

    // Current short-term LUFS. Returns kSilence for effectively-silent input.
    double shortTerm() const { return lufs_; }

    static constexpr double kSilence = -144.0;

private:
    KWeighting kL_;
    KWeighting kR_;
    std::vector<double> msBuf_;
    int msPos_ = 0;
    double msSum_ = 0.0;
    double lufs_ = kSilence;
};

} // namespace cotg::dsp
