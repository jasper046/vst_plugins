#include "HyraxMeterView.h"

#include "HyraxParams.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

#include <algorithm>
#include <cstdio>

using namespace VSTGUI;

namespace cotg::hyrax {

namespace {
constexpr uint32_t kTimerMs = 33;         // ~30 Hz redraw / peak decay
constexpr double kPeakDecayDbPerTick = 0.25; // ~10 dB in ~1.3 s

const CColor kBackColor {28, 28, 30, 255};
const CColor kFrameColor {70, 70, 74, 255};
const CColor kTextColor {170, 170, 174, 255};
const CColor kTickColor {90, 90, 94, 255};

// Bar color, green at low reduction shifting to amber/red as it approaches the
// full scale. frac is 0..1 across the visible range.
CColor barColor(double frac)
{
    frac = std::clamp(frac, 0.0, 1.0);
    const auto lerp = [](double a, double b, double t) {
        return static_cast<uint8_t>(a + (b - a) * t + 0.5);
    };
    return CColor {lerp(80, 210, frac), lerp(200, 120, frac), lerp(110, 60, frac), 255};
}

// Smallest discrete full scale (dB) that still contains peak, clamped 5..15.
double chooseFullScale(double peakDb)
{
    if (peakDb <= 5.0)
        return 5.0;
    if (peakDb <= 10.0)
        return 10.0;
    return 15.0;
}
} // namespace

HyraxMeterView::HyraxMeterView(const CRect& size, int32_t tag)
    : CControl(size, nullptr, tag)
{
    setMouseEnabled(false); // read-only meter
}

double HyraxMeterView::currentDb() const
{
    // Control value is normalized over kGainReductionMaxDb (see the processor).
    return std::clamp(static_cast<double>(getValueNormalized()), 0.0, 1.0) *
           kGainReductionMaxDb;
}

void HyraxMeterView::setValue(float value)
{
    CControl::setValue(value);
    const double cur = currentDb();
    if (cur > peakHoldDb_)
        peakHoldDb_ = cur; // rise instantly; the timer handles the decay
}

void HyraxMeterView::onTimer()
{
    const double cur = currentDb();
    peakHoldDb_ = std::max(cur, peakHoldDb_ - kPeakDecayDbPerTick);
    fullScaleDb_ = chooseFullScale(peakHoldDb_);
    invalid();
}

bool HyraxMeterView::attached(CView* parent)
{
    const bool result = CControl::attached(parent);
    if (result && !timer_)
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { onTimer(); }, kTimerMs);
    return result;
}

bool HyraxMeterView::removed(CView* parent)
{
    timer_ = nullptr;
    return CControl::removed(parent);
}

void HyraxMeterView::draw(CDrawContext* context)
{
    const CRect r = getViewSize();

    // Background + frame.
    context->setFillColor(kBackColor);
    context->setFrameColor(kFrameColor);
    context->setLineWidth(1.0);
    context->drawRect(r, kDrawFilledAndStroked);

    // Layout: a numeric readout on the right, the bar+ticks fill the rest.
    constexpr CCoord kReadoutW = 56.0;
    constexpr CCoord kTickH = 12.0;
    constexpr CCoord kPad = 4.0;

    CRect readout(r);
    readout.left = r.right - kReadoutW;
    readout.inset(kPad, kPad);

    CRect meter(r);
    meter.right = r.right - kReadoutW;
    meter.inset(kPad, kPad);

    CRect bar(meter);
    bar.bottom -= kTickH; // reserve a strip at the bottom for tick labels

    CRect ticks(meter);
    ticks.top = bar.bottom;

    const double fs = fullScaleDb_ > 0.0 ? fullScaleDb_ : 5.0;
    const double cur = currentDb();
    const double frac = std::clamp(cur / fs, 0.0, 1.0);

    // Bar: anchored at the right (0 dB), growing leftward with reduction.
    if (frac > 0.0)
    {
        CRect fill(bar);
        fill.left = bar.right - frac * bar.getWidth();
        context->setFillColor(barColor(frac));
        context->drawRect(fill, kDrawFilled);
    }

    context->setFont(kNormalFontSmall);

    // Tick marks + labels across the visible range (0 dB at right).
    context->setFrameColor(kTickColor);
    context->setFontColor(kTextColor);
    constexpr int kDivs = 5; // 0 .. fs in 5 steps
    for (int i = 0; i <= kDivs; ++i)
    {
        const double t = static_cast<double>(i) / kDivs; // 0 at right
        const CCoord x = bar.right - t * bar.getWidth();
        context->drawLine(CPoint(x, bar.bottom - 3), CPoint(x, bar.bottom));

        const double db = t * fs;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%g", db);
        CRect lbl(x - 16, ticks.top, x + 16, ticks.bottom);
        CHoriTxtAlign align = kCenterText;
        if (i == 0)
        {
            lbl.right = bar.right;
            lbl.left = bar.right - 24;
            align = kRightText;
        }
        else if (i == kDivs)
        {
            lbl.left = bar.left;
            lbl.right = bar.left + 24;
            align = kLeftText;
        }
        context->drawString(buf, lbl, align);
    }

    // Numeric readout of the current reduction (negative, e.g. "-4.2 dB").
    char rbuf[24];
    std::snprintf(rbuf, sizeof(rbuf), "%.1f dB", -cur);
    context->setFontColor(kTextColor);
    context->drawString(rbuf, readout, kRightText);

    setDirty(false);
}

} // namespace cotg::hyrax
