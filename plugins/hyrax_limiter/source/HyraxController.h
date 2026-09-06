#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace cotg::hyrax {

// VST3 edit controller for the Hyrax limiter. Registers the automatable
// parameters and read-only meters, restores state from the processor, and
// requests a latency-changed restart when the look-ahead is edited.
class HyraxController : public Steinberg::Vst::EditController
{
public:
    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new HyraxController());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setParamNormalized(
        Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
};

} // namespace cotg::hyrax
