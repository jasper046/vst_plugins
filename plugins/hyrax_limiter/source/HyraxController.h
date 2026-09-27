#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

namespace cotg::hyrax {

// VST3 edit controller for the Hyrax limiter. Registers the automatable
// parameters and the read-only gain-reduction meter, restores state from the
// processor, requests a latency-changed restart when the look-ahead is edited,
// and serves the VSTGUI editor (sliders with numeric edit boxes plus the custom
// gain-reduction meter view).
class HyraxController : public Steinberg::Vst::EditController,
                        public VSTGUI::VST3EditorDelegate
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

    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

    // VST3EditorDelegate: instantiate the custom gain-reduction meter view.
    VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name,
                                    const VSTGUI::UIAttributes& attributes,
                                    const VSTGUI::IUIDescription* description,
                                    VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;
};

} // namespace cotg::hyrax
