#include "HyraxController.h"

#include "HyraxMeterView.h"
#include "HyraxParams.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/uidescription/uiattributes.h"

#include <cmath>
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
        // StringListParameter leaves info.defaultNormalizedValue at 0, so a host
        // "reset to default" would force the toggle Off regardless of its real
        // default. Record the actual default so reset and the validator agree.
        getInfo().defaultNormalizedValue = defaultNorm;
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

    // OUTPUT STAGE toggles.
    parameters.addParameter(
        new ToggleParameter(STR16("Ferro Saturation"), kFerroSaturation, kFerroSaturationDefaultNorm));
    parameters.addParameter(
        new ToggleParameter(STR16("Soft Clipper"), kSoftClipperEnable, kSoftClipperDefaultNorm));
    parameters.addParameter(
        new ToggleParameter(STR16("Slew Limiter"), kSlewLimiter, kSlewLimiterDefaultNorm));

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

    // Read-only per-channel gain-reduction meters (positive dB). The processor
    // publishes the per-block peak here; the editor's meter view polls both.
    parameters.addParameter(
        makeRange(STR16("Gain Reduction L"), kGainReductionL, STR16("dB"),
                  PRange {0.0, kGainReductionMaxDb, 0.0}, ParameterInfo::kIsReadOnly, 1));
    parameters.addParameter(
        makeRange(STR16("Gain Reduction R"), kGainReductionR, STR16("dB"),
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

    // One analog meter per channel. Which channel comes from the "channel"
    // attribute in the .uidesc ("L"/"R"); the view polls the matching read-only
    // GR parameter directly, so it needs no tag/listener binding. The filmstrip
    // bitmap is resolved from the UIDescription's <bitmaps> section by name.
    if (std::strcmp(name, "HyraxGRMeter") == 0)
    {
        int paramId = kGainReductionL;
        if (const std::string* ch = attributes.getAttributeValue("channel"))
            if (*ch == "R")
                paramId = kGainReductionR;
        CBitmap* filmstrip = description->getBitmap("GainReductionMeter");
        return new HyraxMeterView(rect, this, filmstrip, paramId);
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
            break; // older state with fewer params; keep registered defaults for the rest
        setParamNormalized(static_cast<ParamID>(i), v);
    }
    return kResultTrue;
}

tresult PLUGIN_API HyraxController::setParamNormalized(ParamID tag, ParamValue value)
{
    const ParamValue previous = getParamNormalized(tag);
    const tresult result = EditController::setParamNormalized(tag, value);

    // Only Look-Ahead changes reported latency; ask the host to re-read it. The
    // ferro toggle does NOT change latency (its delay is always incurred), so it
    // must not trigger a latency restart -- that was causing a mid-playback PDC
    // renegotiation and a lasting delay mismatch.
    if (tag == kLookAhead && value != previous && componentHandler)
        componentHandler->restartComponent(kLatencyChanged);

    return result;
}

tresult PLUGIN_API HyraxController::getParamValueByString(ParamID tag, TChar* string,
                                                          ParamValue& valueNormalized)
{
    // Threshold and Ceiling span non-positive dB only. Some hosts (notably
    // REAPER on Linux) swallow the '-' key for their own shortcuts, so a typed
    // negative is impossible, and a bare positive entry would be out of range
    // anyway (clamped to 0). Interpret the typed magnitude as negative dB:
    // "12" -> -12 dB; an explicit "-12" still works where the minus gets through.
    if (tag == kThreshold || tag == kCeiling)
    {
        UString wrapper(string, tstrlen(string));
        double plain = 0.0;
        if (!wrapper.scanFloat(plain))
            return kResultFalse;
        valueNormalized = toNorm(tag == kThreshold ? kThresholdRange : kCeilingRange,
                                 -std::fabs(plain));
        return kResultTrue;
    }
    return EditController::getParamValueByString(tag, string, valueNormalized);
}

} // namespace cotg::hyrax
