#include "HyraxProcessor.h"

#include "HyraxIds.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace cotg::hyrax {

HyraxProcessor::HyraxProcessor()
{
    setControllerClass(kControllerUID);
    initDefaults();
}

void HyraxProcessor::initDefaults()
{
    norm_[kThreshold] = toNorm(kThresholdRange, kThresholdRange.def);
    norm_[kCeiling] = toNorm(kCeilingRange, kCeilingRange.def);
    norm_[kLookAhead] = toNorm(kLookAheadRange, kLookAheadRange.def);
    norm_[kRelease] = toNorm(kReleaseRange, kReleaseRange.def);
    norm_[kStereoLink] = toNorm(kStereoLinkRange, kStereoLinkRange.def);
    norm_[kTruePeak] = kTruePeakDefaultNorm;
    norm_[kBypass] = 0.0; // bypass off by default
    norm_[kFerroSaturation] = kFerroSaturationDefaultNorm;
    norm_[kSoftClipperEnable] = kSoftClipperDefaultNorm;
    norm_[kSlewLimiter] = kSlewLimiterDefaultNorm;
}

void HyraxProcessor::applyParametersToEngine()
{
    Params p;
    p.thresholdDb = toPlain(kThresholdRange, norm_[kThreshold]);
    p.ceilingDb = toPlain(kCeilingRange, norm_[kCeiling]);
    p.lookAheadMs = toPlain(kLookAheadRange, norm_[kLookAhead]);
    p.releaseMs = toPlain(kReleaseRange, norm_[kRelease]);
    p.stereoLinkPct = toPlain(kStereoLinkRange, norm_[kStereoLink]);
    p.truePeak = norm_[kTruePeak] >= 0.5;
    p.ferro = norm_[kFerroSaturation] >= 0.5;
    p.softClip = norm_[kSoftClipperEnable] >= 0.5;
    p.slew = norm_[kSlewLimiter] >= 0.5;
    dsp_.setParameters(p);
}

tresult PLUGIN_API HyraxProcessor::initialize(FUnknown* context)
{
    tresult result = AudioEffect::initialize(context);
    if (result != kResultTrue)
        return result;

    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    return kResultTrue;
}

tresult PLUGIN_API HyraxProcessor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                      SpeakerArrangement* outputs, int32 numOuts)
{
    // Only symmetric stereo in / stereo out is supported.
    if (numIns == 1 && numOuts == 1 && inputs[0] == SpeakerArr::kStereo &&
        outputs[0] == SpeakerArr::kStereo)
    {
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

tresult PLUGIN_API HyraxProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        return kResultTrue;
    return kResultFalse;
}

tresult PLUGIN_API HyraxProcessor::setupProcessing(ProcessSetup& setup)
{
    sampleRate_ = setup.sampleRate;
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API HyraxProcessor::setActive(TBool state)
{
    if (state)
    {
        dsp_.prepare(sampleRate_);
        applyParametersToEngine();
        paramsDirty_ = false;
    }
    return AudioEffect::setActive(state);
}

uint32 PLUGIN_API HyraxProcessor::getLatencySamples()
{
    // Look-ahead in samples for the current parameter value. Matches the engine
    // (floor, minimum of one sample). The controller triggers a latency-changed
    // restart when the Look-Ahead parameter is edited, so the host re-reads this.
    const double la = toPlain(kLookAheadRange, norm_[kLookAhead]);
    const int s = std::max(1, static_cast<int>(std::floor(la * 0.001 * sampleRate_)));
    // Plus the always-on oversampled safety clipper's fixed latency, plus the
    // ferro stage's min-phase oversampler when it is engaged (the controller
    // triggers a latency-changed restart when the ferro toggle flips).
    int extra = cotg::dsp::Oversampler::kLatencySamples;
    if (norm_[kFerroSaturation] >= 0.5)
        extra += cotg::dsp::FerroSaturator::filterLatencySamples();
    return static_cast<uint32>(s + extra);
}

template <typename SampleT>
void HyraxProcessor::processChannels(SampleT** in, SampleT** out, int numChannels,
                                     int32 numSamples)
{
    SampleT* inL = in[0];
    SampleT* inR = numChannels > 1 ? in[1] : in[0];
    SampleT* outL = out[0];
    SampleT* outR = numChannels > 1 ? out[1] : out[0];

    for (int32 i = 0; i < numSamples; ++i)
    {
        double l = static_cast<double>(inL[i]);
        double r = static_cast<double>(inR[i]);
        dsp_.processSample(l, r);
        blockPeakGrDbL_ = std::max(blockPeakGrDbL_, dsp_.gainReductionDbL());
        blockPeakGrDbR_ = std::max(blockPeakGrDbR_, dsp_.gainReductionDbR());
        outL[i] = static_cast<SampleT>(l);
        if (numChannels > 1)
            outR[i] = static_cast<SampleT>(r);
    }
}

tresult PLUGIN_API HyraxProcessor::process(ProcessData& data)
{
    // --- 1) apply incoming parameter changes ---
    if (IParameterChanges* changes = data.inputParameterChanges)
    {
        const int32 count = changes->getParameterCount();
        for (int32 i = 0; i < count; ++i)
        {
            IParamValueQueue* q = changes->getParameterData(i);
            if (!q)
                continue;
            const ParamID id = q->getParameterId();
            if (id >= kNumAutomatable)
                continue;
            if (id == kBypass)
            {
                const int32 bp = q->getPointCount();
                if (bp > 0)
                {
                    ParamValue bv;
                    int32 bso;
                    if (q->getPoint(bp - 1, bso, bv) == kResultTrue)
                    {
                        norm_[kBypass] = bv;
                        bypassed_ = bv >= 0.5;
                    }
                }
                continue;
            }
            const int32 numPoints = q->getPointCount();
            if (numPoints <= 0)
                continue;
            ParamValue value;
            int32 sampleOffset;
            if (q->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
            {
                norm_[id] = value;
                paramsDirty_ = true;
            }
        }
    }

    if (paramsDirty_)
    {
        applyParametersToEngine();
        paramsDirty_ = false;
    }

    // --- 2) audio ---
    blockPeakGrDbL_ = 0.0;
    blockPeakGrDbR_ = 0.0;
    if (data.numSamples > 0 && data.numInputs > 0 && data.numOutputs > 0)
    {
        const int numChannels = std::min(data.inputs[0].numChannels, data.outputs[0].numChannels);
        if (numChannels > 0)
        {
            if (bypassed_)
            {
                // Bypass: copy input straight to output for each channel.
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    if (data.symbolicSampleSize == kSample32)
                        memcpy(data.outputs[0].channelBuffers32[ch],
                               data.inputs[0].channelBuffers32[ch],
                               data.numSamples * sizeof(float));
                    else
                        memcpy(data.outputs[0].channelBuffers64[ch],
                               data.inputs[0].channelBuffers64[ch],
                               data.numSamples * sizeof(double));
                }
            }
            else if (data.symbolicSampleSize == kSample32)
                processChannels(data.inputs[0].channelBuffers32, data.outputs[0].channelBuffers32,
                                numChannels, data.numSamples);
            else
                processChannels(data.inputs[0].channelBuffers64, data.outputs[0].channelBuffers64,
                                numChannels, data.numSamples);
        }
    }

    // --- 3) publish the per-channel gain-reduction meters (0 while bypassed) ---
    if (data.outputParameterChanges)
    {
        const auto publish = [&](ParamID id, double grDb) {
            int32 index = 0;
            if (IParamValueQueue* q = data.outputParameterChanges->addParameterData(id, index))
            {
                const double norm = std::clamp(grDb / kGainReductionMaxDb, 0.0, 1.0);
                int32 pointIndex = 0;
                q->addPoint(0, norm, pointIndex);
            }
        };
        publish(kGainReductionL, bypassed_ ? 0.0 : blockPeakGrDbL_);
        publish(kGainReductionR, bypassed_ ? 0.0 : blockPeakGrDbR_);
    }

    return kResultTrue;
}

tresult PLUGIN_API HyraxProcessor::getState(IBStream* state)
{
    IBStreamer streamer(state, kLittleEndian);
    for (int i = 0; i < kNumAutomatable; ++i)
        streamer.writeDouble(norm_[i]);
    return kResultTrue;
}

tresult PLUGIN_API HyraxProcessor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    for (int i = 0; i < kNumAutomatable; ++i)
    {
        double v = 0.0;
        if (!streamer.readDouble(v))
            break; // older state with fewer params; keep defaults for the rest
        norm_[i] = v;
    }
    paramsDirty_ = true;
    return kResultTrue;
}

} // namespace cotg::hyrax
