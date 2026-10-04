#pragma once

#include "vstgui/lib/controls/cknob.h"

namespace cotg::hyrax {

// A CAnimKnob that resets to the parameter's default value on a double-click.
// Registered as the "HyraxKnob" view class (deriving from CAnimKnob) so the
// .uidesc uses it exactly like CAnimKnob — same bitmap/control-tag attributes,
// and VST3Editor still binds it to its parameter and sets its default value.
class HyraxKnob : public VSTGUI::CAnimKnob
{
public:
    HyraxKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
              VSTGUI::CBitmap* background)
        : VSTGUI::CAnimKnob(size, listener, tag, background)
    {
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override;

    CLASS_METHODS(HyraxKnob, VSTGUI::CAnimKnob)
};

} // namespace cotg::hyrax
