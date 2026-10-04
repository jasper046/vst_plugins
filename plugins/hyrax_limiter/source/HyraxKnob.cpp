#include "HyraxKnob.h"

#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"

using namespace VSTGUI;

namespace cotg::hyrax {

CMouseEventResult HyraxKnob::onMouseDown(CPoint& where, const CButtonState& buttons)
{
    // Double-click (left button) resets the knob to the parameter's default,
    // wrapped in begin/endEdit so the host records a single automation edit.
    if (buttons.isLeftButton() && buttons.isDoubleClick())
    {
        beginEdit();
        setValue(getDefaultValue());
        valueChanged();
        endEdit();
        invalid();
        return kMouseEventHandled;
    }
    return CAnimKnob::onMouseDown(where, buttons);
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
