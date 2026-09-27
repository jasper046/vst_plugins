#pragma once

#include "vstgui/lib/controls/cslider.h"

namespace cotg::hyrax {

// Horizontal linear slider with a code-drawn knob (no bitmap assets): a slim
// groove, an accent fill up to the current value, and a rectangular knob cap
// like a mixer fader. The stock CSlider base handles all mouse and parameter
// behaviour; only the drawing is customized.
class HyraxSlider : public VSTGUI::CSlider
{
public:
    HyraxSlider(const VSTGUI::CRect& size, int32_t tag);

    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS(HyraxSlider, VSTGUI::CSlider)
};

} // namespace cotg::hyrax
