#include "HyraxController.h"

#include "HyraxMeterView.h"
#include "HyraxParams.h"
#include "HyraxSlider.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/uidescription/uiattributes.h"

#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

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

// Toggle parameter that correctly round-trips "Off"↔0.0 and "On"↔1.0.
class ToggleParameter : public StringListParameter
{
public:
    ToggleParameter(const TChar* title, ParamID tag, ParamValue defaultNorm,
                    int32 flags = ParameterInfo::kCanAutomate,
                    const TChar* units = nullptr)
        : StringListParameter(title, tag, units, flags)
    {
        appendString(STR16("Off"));
        appendString(STR16("On"));
        setNormalized(defaultNorm);
    }

    // Override toNormalized so that plain values 0/1 map to 0.0/1.0 instead
    // of the default StringListParameter mapping (which divides by stepCount).
    ParamValue toNormalized(ParamValue plainValue) const override
    {
        return plainValue <= 0.0 ? 0.0 : 1.0;
    }

    ParamValue toPlain(ParamValue _valueNormalized) const override
    {
        return _valueNormalized >= 0.5 ? 1.0 : 0.0;
    }

    OBJ_METHODS(ToggleParameter, StringListParameter)
};

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

    // Toggle: "Off"→0.0, "On"→1.0 — round-trips correctly for the validator's
    // getParamValueByString test.
    parameters.addParameter(
        new ToggleParameter(STR16("True Peak"), kTruePeak, kTruePeakDefaultNorm));

    // Bypass parameter — hosts use this for gapless bypass with latency
    // compensation. Build a ParameterInfo and use addParameter(info) so the
    // kIsBypass flag is set before the Parameter object is created.
    {
        ParameterInfo info {};
        info.id = kBypass;
        Steinberg::UString(info.title, str16BufferSize(String128)).assign(STR16("Bypass"));
        info.flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass;
        info.stepCount = 1; // toggle (0=Off, 1=On)
        info.defaultNormalizedValue = 0.0;
        info.unitId = kRootUnitId;
        parameters.addParameter(info);
    }

    // Read-only gain-reduction meter (positive dB). The processor publishes the
    // per-block peak here; the editor's custom meter view is bound to this tag.
    parameters.addParameter(
        makeRange(STR16("Gain Reduction"), kGainReduction, STR16("dB"),
                  PRange {0.0, kGainReductionMaxDb, 0.0}, ParameterInfo::kIsReadOnly, 1));

    return kResultTrue;
}

IPlugView* PLUGIN_API HyraxController::createView(FIDString name)
{
    if (name && std::strcmp(name, ViewType::kEditor) == 0)
        return new VST3Editor(this, "view", "hyrax_editor.uidesc");
    return nullptr;
}

CView* HyraxController::createCustomView(UTF8StringPtr name, const UIAttributes& attributes,
                                         const IUIDescription* description, VST3Editor* editor)
{
    if (!name)
        return nullptr;

    CPoint origin, size;
    attributes.getPointAttribute("origin", origin);
    attributes.getPointAttribute("size", size);
    const CRect rect(origin, size);

    // Custom views are built with a null listener and (for the meter) no tag
    // applied by the "CView" base attributes, so we wire the control tag and the
    // editor as listener here. verifyView() then binds the control to its
    // parameter (it requires listener == editor and a valid tag).
    if (std::strcmp(name, "HyraxGRMeter") == 0)
    {
        auto* view = new HyraxMeterView(rect, kGainReduction);
        view->setListener(editor);
        return view;
    }

    if (std::strcmp(name, "HyraxSlider") == 0)
    {
        int32_t tag = -1;
        if (const std::string* tagName = attributes.getAttributeValue("control-tag"))
            tag = description->getTagForName(tagName->c_str());
        auto* view = new HyraxSlider(rect, tag);
        view->setListener(editor);
        return view;
    }

    return nullptr;
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
