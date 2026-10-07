#pragma once

#include "vstgui/lib/controls/cknob.h"

namespace cotg::hyrax {

// A CAnimKnob with plain vertical (up/down) mouse dragging instead of VSTGUI's
// default radial/circular gesture, and a double-click reset to the parameter's
// default value. Registered as the "HyraxKnob" view class (deriving from
// CAnimKnob) so the .uidesc uses it exactly like CAnimKnob — same
// bitmap/control-tag attributes, and VST3Editor still binds it to its
// parameter and sets its default value.
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
    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where,
                                        const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseCancel() override;

    CLASS_METHODS(HyraxKnob, VSTGUI::CAnimKnob)

private:
    // Drag state for the vertical editing gesture (CKnobBase's own mouse state
    // stays unused since we bypass its circular-mode handling entirely).
    // dragValue is our own accumulator: it must never be re-read from the
    // control mid-drag, because VST3Editor snaps stepped parameters' control
    // value back to their step grid on every edit.
    VSTGUI::CPoint lastDragPoint;
    float dragValue = 0.f;
    float dragStartValue = 0.f;
};

} // namespace cotg::hyrax
