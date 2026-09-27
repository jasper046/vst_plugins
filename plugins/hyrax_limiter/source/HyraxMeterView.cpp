#include "HyraxMeterView.h"

#include "HyraxParams.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace cotg::hyrax {

namespace {
constexpr uint32_t kTimerMs = 33;         // ~30 Hz redraw / peak update
constexpr double kPeakHoldMs = 1000.0;    // hold the max-reduction line for ~1 s
constexpr double kPeakDecayTauMs = 350.0; // one-pole time constant after hold

const CColor kPanelBack {30, 30, 32, 255};
const CColor kBarBack {20, 20, 22, 255};
const CColor kFrameColor {70, 70, 74, 255};
const CColor kTextColor {170, 170, 174, 255};
const CColor kTickColor {90, 90, 94, 255};
const CColor kPeakColor {235, 235, 130, 255}; // "max reduction" line

// Bar color, green at low reduction shifting to amber/red toward full scale.
CColor barColor(double frac)
{
    frac = std::clamp(frac, 0.0, 1.0);
    const auto lerp = [](double a, double b, double t) {
        return static_cast<uint8_t>(a + (b - a) * t + 0.5);
    };
    return CColor {lerp(80, 210, frac), lerp(200, 120, frac), lerp(110, 60, frac), 255};
}

// Tick divisions on the fixed scale: 0, 3, 6, 9, 12 dB (5 labels).
constexpr int kTickDivs = 4;
} // namespace

HyraxMeterView::HyraxMeterView(const CRect& size, Steinberg::Vst::EditController* controller)
    : CView(size), controller_(controller)
{
}

double HyraxMeterView::readReductionDb(int paramId) const
{
    if (!controller_)
        return 0.0;
    const double norm = controller_->getParamNormalized(static_cast<Steinberg::Vst::ParamID>(paramId));
    return std::clamp(norm, 0.0, 1.0) * kGainReductionMaxDb;
}

void HyraxMeterView::advance(Channel& ch, double cur) const
{
    ch.currentDb = cur;
    if (cur >= ch.peakDb)
    {
        // Rise is instant and (re)arms the hold.
        ch.peakDb = cur;
        ch.holdRemainingMs = kPeakHoldMs;
    }
    else if (ch.holdRemainingMs > 0.0)
    {
        ch.holdRemainingMs -= kTimerMs;
    }
    else
    {
        // Single-sided one-pole decay back toward the current reduction.
        const double coeff = std::exp(-static_cast<double>(kTimerMs) / kPeakDecayTauMs);
        ch.peakDb = cur + (ch.peakDb - cur) * coeff;
    }
}

void HyraxMeterView::onTimer()
{
    advance(left_, readReductionDb(kGainReductionL));
    advance(right_, readReductionDb(kGainReductionR));
    invalid();
}

bool HyraxMeterView::attached(CView* parent)
{
    const bool result = CView::attached(parent);
    if (result && !timer_)
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { onTimer(); }, kTimerMs);
    return result;
}

bool HyraxMeterView::removed(CView* parent)
{
    timer_ = nullptr;
    return CView::removed(parent);
}

void HyraxMeterView::drawBar(CDrawContext* context, const CRect& bar, const Channel& ch) const
{
    const double fs = kGainReductionMeterDb;

    context->setFillColor(kBarBack);
    context->setFrameColor(kFrameColor);
    context->drawRect(bar, kDrawFilledAndStroked);

    // Fill: anchored at the right (0 dB), growing leftward with reduction.
    const double frac = std::clamp(ch.currentDb / fs, 0.0, 1.0);
    if (frac > 0.0)
    {
        CRect fill(bar);
        fill.left = bar.right - frac * bar.getWidth();
        context->setFillColor(barColor(frac));
        context->drawRect(fill, kDrawFilled);
    }

    // "Max reduction" line at the held peak position.
    const double peakFrac = std::clamp(ch.peakDb / fs, 0.0, 1.0);
    if (peakFrac > 0.0)
    {
        const CCoord px = bar.right - peakFrac * bar.getWidth();
        CRect line(px - 1.5, bar.top, px + 1.5, bar.bottom);
        context->setFillColor(kPeakColor);
        context->drawRect(line, kDrawFilled);
    }
}

void HyraxMeterView::draw(CDrawContext* context)
{
    const CRect r = getViewSize();
    context->setDrawMode(kAntiAliasing);
    context->setLineWidth(1.0);

    context->setFillColor(kPanelBack);
    context->drawRect(r, kDrawFilled);

    // Layout: a small gutter on the left for the L/R labels, two stacked bars,
    // and a tick-label row underneath.
    constexpr CCoord kLabelW = 16.0;
    constexpr CCoord kBarH = 13.0;
    constexpr CCoord kGap = 3.0;
    constexpr CCoord kTickH = 12.0;

    const CCoord barsLeft = r.left + kLabelW + 2.0;
    const CCoord barsRight = r.right - 2.0;

    CRect lBar(barsLeft, r.top, barsRight, r.top + kBarH);
    CRect rBar(barsLeft, lBar.bottom + kGap, barsRight, lBar.bottom + kGap + kBarH);

    context->setFont(kNormalFontSmall);
    context->setFontColor(kTextColor);
    context->drawString("L", CRect(r.left, lBar.top, barsLeft - 2.0, lBar.bottom), kCenterText);
    context->drawString("R", CRect(r.left, rBar.top, barsLeft - 2.0, rBar.bottom), kCenterText);

    drawBar(context, lBar, left_);
    drawBar(context, rBar, right_);

    // Tick marks + labels on the fixed scale (0 dB at right, 12 dB at left).
    const CCoord barW = barsRight - barsLeft;
    const CRect tickRow(barsLeft, rBar.bottom + 1.0, barsRight, rBar.bottom + 1.0 + kTickH);
    context->setFrameColor(kTickColor);
    context->setFontColor(kTextColor);
    for (int i = 0; i <= kTickDivs; ++i)
    {
        const double t = static_cast<double>(i) / kTickDivs; // 0 at right
        const CCoord x = barsRight - t * barW;
        context->drawLine(CPoint(x, rBar.bottom - 2.0), CPoint(x, rBar.bottom));

        const int db = static_cast<int>(std::lround(t * kGainReductionMeterDb));
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", db);
        CRect lbl(x - 14.0, tickRow.top, x + 14.0, tickRow.bottom);
        CHoriTxtAlign align = kCenterText;
        if (i == 0)
        {
            lbl.right = barsRight;
            lbl.left = barsRight - 24.0;
            align = kRightText;
        }
        else if (i == kTickDivs)
        {
            lbl.left = barsLeft;
            lbl.right = barsLeft + 24.0;
            align = kLeftText;
        }
        context->drawString(buf, lbl, align);
    }

    setDirty(false);
}

} // namespace cotg::hyrax
