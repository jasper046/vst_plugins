#include "HyraxKnob.h"

#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"

#include <algorithm>

using namespace VSTGUI;

namespace cotg::hyrax {

CMouseEventResult HyraxKnob::onMouseDown(CPoint& where, const CButtonState& buttons)
{
    if (!buttons.isLeftButton())
        return kMouseEventNotHandled;

    // Double-click resets the knob to the parameter's default, wrapped in
    // begin/endEdit so the host records a single automation edit.
    if (buttons.isDoubleClick())
    {
        beginEdit();
        setValue(getDefaultValue());
        valueChanged();
        endEdit();
        invalid();
        return kMouseEventHandled;
    }

    // Start a vertical drag instead of CKnobBase's radial gesture (which is
    // driven by the frame's knob mode, defaulting to kCircularMode). The value
    // only changes as the mouse moves — the click position itself is ignored.
    invalidMouseWheelEditTimer(this);
    beginEdit();
    lastDragPoint = where;
    dragValue = dragStartValue = getValue();
    return kMouseEventHandled;
}

CMouseEventResult HyraxKnob::onMouseMoved(CPoint& where, const CButtonState& buttons)
{
    if (!buttons.isLeftButton() || !isEditing())
        return kMouseEventNotHandled;

    if (where == lastDragPoint)
        return kMouseEventHandled;

    // Plain up/down adjustment: dragging up raises the value, dragging down
    // lowers it. Shift (kZoomModifier) gives fine control.
    float range = getKnobRange();
    if (buttons & kZoomModifier)
        range *= getZoomFactor();
    const float coef = (getMax() - getMin()) / range;

    const CCoord diff = lastDragPoint.y - where.y;
    lastDragPoint = where;

    // Accumulate in our own dragValue instead of reading back getValue():
    // stepped parameters (e.g. Look Ahead in whole ms) get their control value
    // snapped back to the step grid by VST3Editor on every edit, which would
    // cancel out incremental deltas smaller than one step (dead or erratic
    // knob). dragValue is immune to that snap-back.
    const float prev = dragValue;
    dragValue += static_cast<float>(diff) * coef;
    dragValue = std::clamp(dragValue, getMin(), getMax());

    if (dragValue != prev)
    {
        setValue(dragValue);
        valueChanged();
        if (isDirty())
            invalid();
    }
    return kMouseEventHandled;
}

CMouseEventResult HyraxKnob::onMouseUp(CPoint& /*where*/, const CButtonState& /*buttons*/)
{
    // CKnobBase::onMouseUp would also work, but its mouse-editing state is
    // never created here, so just close our edit gesture.
    if (isEditing())
        endEdit();
    return kMouseEventHandled;
}

CMouseEventResult HyraxKnob::onMouseCancel()
{
    if (isEditing())
    {
        dragValue = dragStartValue;
        setValue(dragStartValue);
        valueChanged();
        invalid();
        endEdit();
    }
    return kMouseEventHandled;
}

namespace {
// Registers "HyraxKnob" as a view class based on CAnimKnob. Using CAnimKnob as
// the base view name makes the factory apply the inherited attributes (bitmap,
// control-tag, size, ...) so nothing else in the .uidesc has to change.
class HyraxKnobCreator : public ViewCreatorAdapter
{
public:
    HyraxKnobCreator() { UIViewFactory::registerViewCreator(*this); }

    IdStringPtr getViewName() const override { return "HyraxKnob"; }
    IdStringPtr getBaseViewName() const override { return "CAnimKnob"; }
    UTF8StringPtr getDisplayName() const override { return "Hyrax Knob"; }

    CView* create(const UIAttributes& /*attributes*/,
                  const IUIDescription* /*description*/) const override
    {
        return new HyraxKnob(CRect(0, 0, 0, 0), nullptr, -1, nullptr);
    }
};

// Static instance registers the creator at module load.
HyraxKnobCreator gHyraxKnobCreator;
} // namespace

} // namespace cotg::hyrax
