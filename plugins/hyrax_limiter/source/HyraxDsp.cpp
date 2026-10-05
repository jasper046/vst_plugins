#include "HyraxDsp.h"

#include <algorithm>
#include <cmath>

namespace cotg::hyrax {

namespace {
constexpr double kPi = 3.14159265358979323846;

double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
} // namespace

void HyraxDsp::prepare(double sampleRate, double maxLookAheadMs)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

    // The constant output-delay target = the top of the Look-Ahead range.
    maxLookSamples_ = std::max(1, static_cast<int>(std::floor(maxLookAheadMs * 0.001 * sampleRate_)));

    // Ring buffers sized for the maximum look-ahead (+ headroom over the range).
    laMax_ = maxLookSamples_ + 4;
    left_.resize(laMax_);
    right_.resize(laMax_);
    peak_.resize(laMax_);

    // Trailing compensation delay: holds up to maxLookSamples_ so (max - current)
    // pads the output back to the constant latency.
    laCompL_.resize(maxLookSamples_ + 1);
    laCompR_.resize(maxLookSamples_ + 1);

    // Matched delays for the guard-off bypass path (= the oversampler's group
    // delay, so toggling the guard never shifts timing or latency).
    scBypassL_.resize(cotg::dsp::Oversampler::kLatencySamples + 1);
    scBypassR_.resize(cotg::dsp::Oversampler::kLatencySamples + 1);

    // Matched delay (= ferro's latency) for the ferro-off path.
    ferroBypassL_.resize(cotg::dsp::FerroSaturator::filterLatencySamples() + 1);
    ferroBypassR_.resize(cotg::dsp::FerroSaturator::filterLatencySamples() + 1);

    // OUTPUT STAGE effects, configured to their frozen production settings (see
    // FerroSaturator.h / SlewLimiter.h). The ferro ceiling reference is a
    // placeholder here; setParameters() updates it from the Ceiling slider before
    // any audio runs.
    namespace dsp = cotg::dsp;
    ferro_.configure(sampleRate_, dsp::FerroSaturator::kDriveDbDefault,
                     dsp::FerroSaturator::kKneeDefault, dsp::FerroSaturator::kCeilingDefault,
                     dsp::FerroSaturator::kAsymDefault, /*ceilingDbfs*/ 0.0,
                     dsp::FerroSaturator::kHfBypassHzDefault);
    slew_.configure(sampleRate_, dsp::SlewLimiter::kPctDefault,
                    dsp::SlewLimiter::kKneeFracDefault, dsp::SlewLimiter::kHpHzDefault);

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
    grDbL_ = 0.0;
    grDbR_ = 0.0;

    osL1_ = 0.0;
    osR1_ = 0.0;

    osL_.reset();
    osR_.reset();
    ferro_.reset();
    slew_.reset();
    scBypassL_.clear();
    scBypassR_.clear();
    ferroBypassL_.clear();
    ferroBypassR_.clear();
    laCompL_.clear();
    laCompR_.clear();
}

void HyraxDsp::setParameters(const Params& p)
{
    thresh_ = dbToLin(p.thresholdDb);
    ceiling_ = dbToLin(p.ceilingDb);
    makeup_ = ceiling_ / thresh_;

    // Soft clipper as a transparent 0 dBFS safety guard (ceiling 0 dBFS, fixed
    // 0.5 dB knee), independent of the Ceiling slider. The limiter already seats
    // the program at the Ceiling; the guard only bends genuine 0 dBFS overs,
    // rather than rounding every limited peak down (which added harshness).
    softClip_.configure(0.0, kSoftClipGuardKneeDb);

    // OUTPUT STAGE toggles + ferro's ceiling reference (the normalized domain
    // tracks the Ceiling slider). setCeilingDbfs is state-preserving, so this is
    // safe every block.
    ferroEnabled_ = p.ferro;
    softClipEnabled_ = p.softClip;
    slewEnabled_ = p.slew;
    ferro_.setCeilingDbfs(p.ceilingDb);

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

    // --- stereo link ---
    const double dlOwn = absl <= thresh_ ? 0.0 : 1.0 - thresh_ / absl;
    const double drOwn = absr <= thresh_ ? 0.0 : 1.0 - thresh_ / absr;
    const double gl = 1.0 - (link_ * dFinal + (1.0 - link_) * dlOwn);
    const double gr = 1.0 - (link_ * dFinal + (1.0 - link_) * drOwn);

    // Metering: per-channel gain reduction in positive dB (makeup is a separate
    // constant gain and is not counted). gl/gr are limiting gains in (0, 1].
    grDbL_ = gl < 1.0 && gl > 0.0 ? -20.0 * std::log10(gl) : 0.0;
    grDbR_ = gr < 1.0 && gr > 0.0 ? -20.0 * std::log10(gr) : 0.0;

    // --- read delayed audio and apply gain + makeup ---
    double outL = left_.back(lookSamples_) * gl * makeup_;
    double outR = right_.back(lookSamples_) * gr * makeup_;

    left_.advance();
    right_.advance();
    peak_.advance();

    // --- OUTPUT STAGE: ferro -> slew -> soft-clip 0 dBFS guard (soft clip last
    // so it owns the final ceiling). Ferro and slew are optional effects; the
    // soft clip defaults on. ---

    // Ferro-magnetic saturation: ceiling-normalized warmth on the low band, HF
    // kept clean. Its latency is always incurred -- when disabled the signal is
    // routed through a matched delay instead of the saturator -- so the toggle
    // never changes reported latency (no mid-playback PDC renegotiation). The
    // delay is fed every sample so it stays warm.
    ferroBypassL_.write(outL);
    ferroBypassR_.write(outR);
    const double ferroBypassOutL = ferroBypassL_.back(ferro_.latencySamples());
    const double ferroBypassOutR = ferroBypassR_.back(ferro_.latencySamples());
    ferroBypassL_.advance();
    ferroBypassR_.advance();
    if (ferroEnabled_)
    {
        ferro_.process(outL, outR);
    }
    else
    {
        outL = ferroBypassOutL;
        outR = ferroBypassOutR;
    }

    // Flat slew-rate limiter: content-adaptive HF smoothness. No added latency.
    if (slewEnabled_)
        slew_.process(outL, outR);

    // Feed the matched bypass delays (the guard-off path) every sample so they
    // stay in sync and toggling the guard is click-free.
    scBypassL_.write(outL);
    scBypassR_.write(outR);
    const double bypassL = scBypassL_.back(cotg::dsp::Oversampler::kLatencySamples);
    const double bypassR = scBypassR_.back(cotg::dsp::Oversampler::kLatencySamples);
    scBypassL_.advance();
    scBypassR_.advance();

    // Soft-clip 0 dBFS guard: 4x oversampled soft clip so genuine inter-sample
    // peaks that escaped the limiter are caught. The oversampler runs
    // unconditionally so it stays warm and latency is constant; its output is
    // used only when the guard is enabled.
    double up[cotg::dsp::Oversampler::kOS];
    osL_.upsample(outL, up);
    if (softClipEnabled_)
        for (double& s : up)
            s = softClip_.clip(s);
    const double clippedL = osL_.downsample(up);

    osR_.upsample(outR, up);
    if (softClipEnabled_)
        for (double& s : up)
            s = softClip_.clip(s);
    const double clippedR = osR_.downsample(up);

    if (softClipEnabled_)
    {
        // Absolute sample-domain guarantee: no output sample exceeds 0 dBFS. The
        // decimation filter can leave a hair of overshoot past what the soft clip
        // caught; this hard clamp (which engages essentially never) makes "the
        // output never actually clips" a hard guarantee. Inter-sample (true)
        // peaks are strongly reduced by the oversampled soft clip but, being a
        // clipper rather than a true-peak limiter, not guaranteed below 0 dBTP.
        outL = std::clamp(clippedL, -1.0, 1.0);
        outR = std::clamp(clippedR, -1.0, 1.0);
    }
    else
    {
        // Guard off: bypass the oversampler entirely via the matched delay, so
        // the output stage is fully transparent (no HF colouring) rather than
        // passing the signal through the oversampler's low-pass round-trip.
        outL = bypassL;
        outR = bypassR;
    }

    // Trailing look-ahead compensation: delay the finished output by
    // (maxLookSamples_ - lookSamples_) so the total output delay is always
    // maxLookSamples_, independent of the current Look-Ahead setting. Fed every
    // sample; moving Look-Ahead only steps the read offset, so reported latency
    // never changes and the host's delay compensation stays valid.
    laCompL_.write(outL);
    laCompR_.write(outR);
    const int comp = maxLookSamples_ - lookSamples_;
    outL = laCompL_.back(comp);
    outR = laCompR_.back(comp);
    laCompL_.advance();
    laCompR_.advance();

    left = outL;
    right = outR;
}

} // namespace cotg::hyrax
