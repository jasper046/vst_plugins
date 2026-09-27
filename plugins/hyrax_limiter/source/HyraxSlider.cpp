#include "HyraxSlider.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>

using namespace VSTGUI;

namespace cotg::hyrax {

namespace {
constexpr CCoord kKnobW = 10.0;  // knob cap width, px
constexpr CCoord kGrooveH = 4.0; // groove thickness, px

const CColor kGrooveColor {42, 42, 46, 255};
const CColor kGrooveFrame {70, 70, 74, 255};
const CColor kFillColor {90, 200, 120, 255};
const CColor kKnobColor {210, 210, 214, 255};
const CColor kKnobFrame {28, 28, 30, 255};
} // namespace

HyraxSlider::HyraxSlider(const CRect& size, int32_t tag)
    : CSlider(size, nullptr, tag, 0, 0, nullptr, nullptr)
{
    setDrawStyle(0); // we draw everything ourselves
    // Reserve the knob width as the handle size so the base class maps clicks
    // and the handle position across [0 .. width - knobW], then recompute range.
    setHandleSizePrivate(kKnobW, size.getHeight());
    setViewSize(getViewSize(), true);
}

void HyraxSlider::draw(CDrawContext* context)
{
    const CRect r = getViewSize();
    context->setDrawMode(kAntiAliasing);
    context->setLineWidth(1.0);

    // Groove: a slim horizontal track centered vertically, inset by half a knob
    // at each end so the knob cap stays flush at the extremes.
    CRect groove(r);
    groove.top = r.top + (r.getHeight() - kGrooveH) / 2.0;
    groove.bottom = groove.top + kGrooveH;
    groove.left += kKnobW / 2.0;
    groove.right -= kKnobW / 2.0;
    context->setFillColor(kGrooveColor);
    context->setFrameColor(kGrooveFrame);
    context->drawRect(groove, kDrawFilledAndStroked);

    // Knob position from the base class (absolute coords).
    const CRect handle = calculateHandleRect(getValueNormalized());
    const CCoord knobCenter = handle.left + kKnobW / 2.0;

    // Accent fill from the groove's left up to the knob center.
    CRect fill(groove);
    fill.right = std::clamp(knobCenter, groove.left, groove.right);
    if (fill.getWidth() > 0.5)
    {
        context->setFillColor(kFillColor);
        context->drawRect(fill, kDrawFilled);
    }

    // Knob cap: a full-height rectangle at the handle position.
    CRect knob(handle.left, r.top, handle.left + kKnobW, r.bottom);
    knob.inset(0.0, 1.0);
    context->setFillColor(kKnobColor);
    context->setFrameColor(kKnobFrame);
    context->drawRect(knob, kDrawFilledAndStroked);

    setDirty(false);
}

} // namespace cotg::hyrax
