#include "HyraxController.h"

#include "HyraxParams.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace cotg::hyrax {

namespace {
// `precision` is the number of decimal digits shown (and accepted) in the
// host's generic UI text field. `stepCount` > 0 makes the parameter discrete
// (e.g. integer steps); 0 keeps it continuous.
RangeParameter* makeRange(const TChar* title, ParamID tag, const TChar* units, const PRange& r,
                          int32 flags, int32 precision, int32 stepCount = 0)
{
    auto* p = new RangeParameter(title, tag, units, r.min, r.max, r.def, stepCount, flags);
    p->setPrecision(precision);
    return p;
}

// Number of unit (integer) steps spanning a range, e.g. 50..6000 ms -> 5950.
int32 integerSteps(const PRange& r)
{
    return static_cast<int32>(r.max - r.min);
}
} // namespace

tresult PLUGIN_API HyraxController::initialize(FUnknown* context)
{
    tresult result = EditController::initialize(context);
    if (result != kResultTrue)
        return result;

    parameters.addParameter(
        makeRange(STR16("Threshold"), kThreshold, STR16("dB"), kThresholdRange, ParameterInfo::kCanAutomate, 1));
    parameters.addParameter(
        makeRange(STR16("Ceiling"), kCeiling, STR16("dB"), kCeilingRange, ParameterInfo::kCanAutomate, 1));
    parameters.addParameter(
        makeRange(STR16("Look Ahead"), kLookAhead, STR16("ms"), kLookAheadRange, ParameterInfo::kCanAutomate, 0, integerSteps(kLookAheadRange)));
    parameters.addParameter(
        makeRange(STR16("Release"), kRelease, STR16("ms"), kReleaseRange, ParameterInfo::kCanAutomate, 0, integerSteps(kReleaseRange)));
    parameters.addParameter(
        makeRange(STR16("Stereo Link"), kStereoLink, STR16("%"), kStereoLinkRange, ParameterInfo::kCanAutomate, 0, integerSteps(kStereoLinkRange)));

    // Toggle: stepCount 1 makes hosts render a checkbox/button rather than a
    // slider. Default matches the processor (True Peak on).
    parameters.addParameter(STR16("True Peak"), nullptr, 1, kTruePeakDefaultNorm,
                            ParameterInfo::kCanAutomate, kTruePeak);

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
