#include "HyraxMeterView.h"

#include "HyraxParams.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <cmath>

using namespace VSTGUI;

namespace cotg::hyrax {

namespace {
constexpr uint32_t kTimerMs = 33;         // ~30 Hz redraw / needle update
constexpr double kBallisticTauMs = 150.0; // one-pole needle smoothing (VU feel)

// One frame of GainReductionMeter.png. The asset is a horizontal filmstrip of
// 128 px-wide frames (3968 x 94 = 31 frames); the frame count is derived from
// the bitmap width at draw time so re-exporting with more/fewer frames just
// works as long as the frame width stays 128.
constexpr CCoord kFrameWidth = 128.0;
} // namespace

HyraxMeterView::HyraxMeterView(const CRect& size, Steinberg::Vst::EditController* controller,
                               CBitmap* filmstrip, int paramId)
    : CView(size), controller_(controller), filmstrip_(filmstrip), paramId_(paramId)
{
}

double HyraxMeterView::readReductionDb() const
{
    if (!controller_)
        return 0.0;
    const double norm = controller_->getParamNormalized(static_cast<Steinberg::Vst::ParamID>(paramId_));
    return std::clamp(norm, 0.0, 1.0) * kGainReductionMaxDb;
}

void HyraxMeterView::onTimer()
{
    const double target = readReductionDb();
    // One-pole smoothing toward the current reduction for VU-like needle motion.
    const double coeff = std::exp(-static_cast<double>(kTimerMs) / kBallisticTauMs);
    displayedDb_ = target + (displayedDb_ - target) * coeff;
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

void HyraxMeterView::draw(CDrawContext* context)
{
    if (!filmstrip_)
    {
        setDirty(false);
        return;
    }

    const int frameCount = std::max(1, static_cast<int>(std::lround(filmstrip_->getWidth() / kFrameWidth)));

    // Map reduction (0 .. visible full scale) to a frame: needle rests at the
    // leftmost frame with no reduction and sweeps right as reduction grows.
    const double frac = std::clamp(displayedDb_ / kGainReductionMeterDb, 0.0, 1.0);
    const int frame = std::clamp(static_cast<int>(std::lround(frac * (frameCount - 1))), 0, frameCount - 1);

    // Standard filmstrip draw: shift the source left by frame*frameWidth and let
    // the view rect (sized to one frame) clip to that frame. No scaling.
    filmstrip_->draw(context, getViewSize(), CPoint(frame * kFrameWidth, 0));

    setDirty(false);
}

} // namespace cotg::hyrax
