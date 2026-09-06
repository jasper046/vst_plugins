#pragma once

#include <vector>

namespace cotg::dsp {

// Single-channel circular delay line. Mirrors the JSFX look-ahead ring-buffer
// idiom: write() stores at the current position without advancing, back(i)
// reads i samples back (back(0) is the just-written sample), and advance()
// moves the write head forward one sample.
//
// Several RingBuffers created with the same length and advanced in lockstep
// keep identical write positions, so a set of them (e.g. left / right / peak)
// can be read with matching offsets.
class RingBuffer
{
public:
    void resize(int n)
    {
        buf_.assign(n > 0 ? n : 1, 0.0);
        wpos_ = 0;
    }

    void clear()
    {
        std::fill(buf_.begin(), buf_.end(), 0.0);
        wpos_ = 0;
    }

    int size() const { return static_cast<int>(buf_.size()); }

    void write(double x) { buf_[wpos_] = x; }

    // i samples back from the write head; i in [0, size()).
    double back(int i) const
    {
        int r = wpos_ - i;
        if (r < 0)
            r += size();
        return buf_[r];
    }

    void advance()
    {
        if (++wpos_ >= size())
            wpos_ = 0;
    }

private:
    std::vector<double> buf_;
    int wpos_ = 0;
};

} // namespace cotg::dsp
