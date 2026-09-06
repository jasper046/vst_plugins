#include "HyraxController.h"

#include "HyraxParams.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace cotg::hyrax {

namespace {
RangeParameter* makeRange(const TChar* title, ParamID tag, const TChar* units, const PRange& r,
                          int32 flags)
{
    return new RangeParameter(title, tag, units, r.min, r.max, r.def, 0, flags);
}
} // namespace

tresult PLUGIN_API HyraxController::initialize(FUnknown* context)
{
    tresult result = EditController::initialize(context);
    if (result != kResultTrue)
        return result;

    parameters.addParameter(
        makeRange(STR16("Threshold"), kThreshold, STR16("dB"), kThresholdRange, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        makeRange(STR16("Ceiling"), kCeiling, STR16("dB"), kCeilingRange, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        makeRange(STR16("Look Ahead"), kLookAhead, STR16("ms"), kLookAheadRange, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        makeRange(STR16("Release"), kRelease, STR16("ms"), kReleaseRange, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        makeRange(STR16("Stereo Link"), kStereoLink, STR16("%"), kStereoLinkRange, ParameterInfo::kCanAutomate));

    auto* truePeak = new StringListParameter(STR16("True Peak"), kTruePeak);
    truePeak->appendString(STR16("Off"));
    truePeak->appendString(STR16("On"));
    parameters.addParameter(truePeak);

    parameters.addParameter(
        makeRange(STR16("Target LUFS"), kTargetLufs, STR16("LUFS"), kTargetLufsRange, ParameterInfo::kCanAutomate));

    auto* sense = new StringListParameter(STR16("SENSE"), kSense);
    sense->appendString(STR16("Off"));
    sense->appendString(STR16("On"));
    parameters.addParameter(sense);

    // Read-only output meters (visible in the generic UI, driven by the processor).
    parameters.addParameter(
        makeRange(STR16("Gain Reduction"), kGrMeter, STR16("dB"), kGrMeterRange, ParameterInfo::kIsReadOnly));
    parameters.addParameter(
        makeRange(STR16("Short-term LUFS"), kLufsMeter, STR16("LUFS"), kLufsMeterRange, ParameterInfo::kIsReadOnly));

    // Defaults for the toggles (match the processor's defaults).
    setParamNormalized(kTruePeak, kTruePeakDefaultNorm);
    setParamNormalized(kSense, kSenseDefaultNorm);

    return kResultTrue;
}

tresult PLUGIN_API HyraxController::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    for (int i = 0; i < kNumAutomatable; ++i)
    {
        double v = 0.0;
        if (!streamer.readDouble(v))
            return kResultFalse;
        setParamNormalized(static_cast<ParamID>(i), v);
    }
    return kResultTrue;
}

tresult PLUGIN_API HyraxController::setParamNormalized(ParamID tag, ParamValue value)
{
    const ParamValue previous = getParamNormalized(tag);
    const tresult result = EditController::setParamNormalized(tag, value);

    // Changing the look-ahead changes reported latency; ask the host to re-read
    // it (the processor reports latency from this parameter).
    if (tag == kLookAhead && value != previous && componentHandler)
        componentHandler->restartComponent(kLatencyChanged);

    return result;
}

} // namespace cotg::hyrax
