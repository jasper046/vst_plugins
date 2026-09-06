#include "HyraxDsp.h"

#include <algorithm>
#include <cmath>

namespace cotg::hyrax {

namespace {
constexpr double kPi = 3.14159265358979323846;

double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
} // namespace

void HyraxDsp::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

    // Ring buffers sized for the maximum look-ahead (25 ms, generous headroom).
    laMax_ = static_cast<int>(std::ceil(0.025 * sampleRate_)) + 4;
    left_.resize(laMax_);
    right_.resize(laMax_);
    peak_.resize(laMax_);

    lufs_.configure(sampleRate_, 3.0);

    // GR meter release toward unity (matches JSFX gr_meter_decay = exp(1/srate)).
    grDecay_ = std::exp(1.0 / sampleRate_);

    senseUpdateEvery_ = std::max(1, static_cast<int>(std::floor(sampleRate_ / 20.0)));

    reset();
}

void HyraxDsp::reset()
{
    left_.clear();
    right_.clear();
    peak_.clear();

    dAtk_ = 0.0;
    dHold_ = 0.0;
    relLp1_ = 0.0;
    relLp2_ = 0.0;
    holdCtr_ = 0;

    osL1_ = 0.0;
    osR1_ = 0.0;

    grMeter_ = 1.0;

    lufs_.reset();

    senseUpdateCtr_ = 0;
    // Note: senseOffsetDb_ is intentionally NOT cleared here so the SENSE
    // offset persists across transport restarts (matching the JSFX, which
    // freezes its threshold rather than snapping back).
}

void HyraxDsp::updateThreshold()
{
    const double eff =
        std::clamp(userThresholdDb_ + senseOffsetDb_, kThresholdMinDb, kThresholdMaxDb);
    thresh_ = dbToLin(eff);
    makeup_ = ceiling_ / thresh_;
}

double HyraxDsp::effectiveThresholdDb() const
{
    return std::clamp(userThresholdDb_ + senseOffsetDb_, kThresholdMinDb, kThresholdMaxDb);
}

void HyraxDsp::setParameters(const Params& p)
{
    userThresholdDb_ = p.thresholdDb;
    ceiling_ = dbToLin(p.ceilingDb);
    updateThreshold();

    // Look-ahead in samples, clamped to the allocated buffer.
    int look = static_cast<int>(std::floor(p.lookAheadMs * 0.001 * sampleRate_));
    look = std::max(1, look);
    lookSamples_ = std::min(look, laMax_ - 1);

    const double releaseMs = std::max(p.releaseMs, 1.0);
    link_ = std::clamp(p.stereoLinkPct * 0.01, 0.0, 1.0);
    truePeak_ = p.truePeak;

    holdSamples_ = std::max(1, static_cast<int>(std::floor(kHoldMs * 0.001 * sampleRate_)));

    // First-order Butterworth (== one-pole) coefficients: a = exp(-2*pi*fc/fs).
    holdA_ = std::exp(-2.0 * kPi * kHoldLpHz / sampleRate_);
    const double relFc = kReleaseLpNum / releaseMs;
    relA_ = std::exp(-2.0 * kPi * relFc / sampleRate_);

    // Attack smoothing pole scaled to the look-ahead window length.
    atkA_ = std::exp(-kAttackSmooth / std::max(lookSamples_, 1));

    senseOn_ = p.senseOn;
    targetLufs_ = p.targetLufs;
}

void HyraxDsp::processSample(double& left, double& right)
{
    const double inL = left;
    const double inR = right;
    const double absl = std::abs(inL);
    const double absr = std::abs(inR);
    double detect = std::max(absl, absr);

    // --- true-peak (inter-sample) estimate via 4x linear interpolation ---
    if (truePeak_)
    {
        double ip = 0.25;
        for (int k = 0; k < 3; ++k)
        {
            const double il = osL1_ + (inL - osL1_) * ip;
            const double ir = osR1_ + (inR - osR1_) * ip;
            detect = std::max(detect, std::max(std::abs(il), std::abs(ir)));
            ip += 0.25;
        }
        osL1_ = inL;
        osR1_ = inR;
    }

    // --- write into the look-ahead ring buffers ---
    left_.write(inL);
    right_.write(inR);
    peak_.write(detect);

    // --- sliding maximum over the look-ahead window ---
    double winMax = 0.0;
    for (int i = 0; i <= lookSamples_; ++i)
        winMax = std::max(winMax, peak_.back(i));

    // Everything below works in the "flipped" reduction domain d = 1 - gain
    // (0 = no reduction). The three stages each produce a reduction amount;
    // they are combined by taking the most reduction, then flipped to a gain.

    // hard-clip reduction for this windowed peak
    const double hcGain = winMax <= thresh_ ? 1.0 : thresh_ / winMax;
    const double dHard = 1.0 - hcGain;

    // attack: rises instantly, decays via the smoothing pole
    if (dHard > dAtk_)
        dAtk_ = dHard;
    else
        dAtk_ = dHard + atkA_ * (dAtk_ - dHard);

    // hold: keep peak reduction for holdSamples_, then let it fall
    if (dHard >= dHold_)
    {
        dHold_ = dHard;
        holdCtr_ = 0;
    }
    else if (++holdCtr_ > holdSamples_)
    {
        dHold_ = dHard + holdA_ * (dHold_ - dHard);
    }

    // release: two cascaded one-pole low-passes fed by the greater of raw/held
    const double relIn = std::max(dHard, dHold_);
    relLp1_ = relIn + relA_ * (relLp1_ - relIn);
    relLp2_ = relLp1_ + relA_ * (relLp2_ - relLp1_);
    const double dRel = std::max(dHold_, relLp2_);

    // combine: most reduction wins, then flip to gain
    const double dFinal = std::max(dHard, std::max(dAtk_, dRel));
    const double gain = 1.0 - dFinal;

    // --- stereo link ---
    const double dlOwn = absl <= thresh_ ? 0.0 : 1.0 - thresh_ / absl;
    const double drOwn = absr <= thresh_ ? 0.0 : 1.0 - thresh_ / absr;
    const double gl = 1.0 - (link_ * dFinal + (1.0 - link_) * dlOwn);
    const double gr = 1.0 - (link_ * dFinal + (1.0 - link_) * drOwn);

    // --- read delayed audio and apply gain + makeup ---
    const double outL = left_.back(lookSamples_) * gl * makeup_;
    const double outR = right_.back(lookSamples_) * gr * makeup_;

    left_.advance();
    right_.advance();
    peak_.advance();

    left = outL;
    right = outR;

    // --- short-term LUFS on the output ---
    lufs_.process(outL, outR);

    // --- SENSE closed loop: ride the threshold toward the target LUFS ---
    if (++senseUpdateCtr_ >= senseUpdateEvery_)
    {
        senseUpdateCtr_ = 0;
        const double lufsSt = lufs_.shortTerm();

        if (senseOn_ && lufsSt > -70.0)
        {
            const double err = targetLufs_ - lufsSt;
            const double maxStep = kSenseRateDb * senseUpdateEvery_ / sampleRate_;
            const double step = std::clamp(err * kSenseGain, -maxStep, maxStep);
            // louder (err > 0) needs a LOWER threshold -> more negative offset
            double newOffset = senseOffsetDb_ - step;
            // clamp the effective threshold into range, folding the clamp back
            // into the offset so it can't wind up past the limits
            const double eff =
                std::clamp(userThresholdDb_ + newOffset, kThresholdMinDb, kThresholdMaxDb);
            newOffset = eff - userThresholdDb_;
            if (newOffset != senseOffsetDb_)
            {
                senseOffsetDb_ = newOffset;
                updateThreshold();
            }
        }
    }

    // --- gain reduction meter (smoothed, releases toward unity) ---
    if (gain < grMeter_)
        grMeter_ = gain;
    else
    {
        grMeter_ *= grDecay_;
        if (grMeter_ > 1.0)
            grMeter_ = 1.0;
    }
}

double HyraxDsp::gainReductionDb() const
{
    return grMeter_ > 0.0 ? 20.0 * std::log10(grMeter_) : -150.0;
}

} // namespace cotg::hyrax
