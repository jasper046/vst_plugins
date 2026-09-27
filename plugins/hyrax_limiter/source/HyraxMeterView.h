#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"

namespace Steinberg {
namespace Vst {
class EditController;
}
} // namespace Steinberg

namespace cotg::hyrax {

// Stereo gain-reduction meter for the Hyrax editor. Two horizontal bars (L over
// R), each anchored at 0 dB on the right and growing leftward with reduction, on
// a fixed full scale (kGainReductionMeterDb). Each bar carries a thick "max
// reduction" line: a peak detector that jumps instantly to the greatest
// reduction, holds for ~1 s, then converges back toward the current reduction
// via a one-pole (single-sided) decay.
//
// Rather than binding to a single control tag, the view polls both read-only
// gain-reduction parameters from the controller on its timer, which also drives
// the hold/decay and redraw.
class HyraxMeterView : public VSTGUI::CView
{
public:
    HyraxMeterView(const VSTGUI::CRect& size, Steinberg::Vst::EditController* controller);

    void draw(VSTGUI::CDrawContext* context) override;
    bool attached(VSTGUI::CView* parent) override;
    bool removed(VSTGUI::CView* parent) override;

    CLASS_METHODS(HyraxMeterView, VSTGUI::CView)

private:
    // One bar's state: the current reduction and its held peak (dB, positive).
    struct Channel
    {
        double currentDb = 0.0;
        double peakDb = 0.0;
        double holdRemainingMs = 0.0;
    };

    double readReductionDb(int paramId) const; // current reduction from the controller
    void advance(Channel& ch, double currentDb) const; // hold + one-pole decay
    void drawBar(VSTGUI::CDrawContext* context, const VSTGUI::CRect& bar, const Channel& ch) const;
    void onTimer();

    Steinberg::Vst::EditController* controller_ = nullptr;
    Channel left_;
    Channel right_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace cotg::hyrax
