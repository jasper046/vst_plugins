#pragma once

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cvstguitimer.h"

namespace cotg::hyrax {

// Horizontal gain-reduction meter for the Hyrax editor. Bound (by control tag)
// to the read-only "Gain Reduction" parameter the processor publishes: the
// control value is the reduction normalized over kGainReductionMaxDb.
//
// The bar is anchored at 0 dB on the right edge and grows leftward with
// increasing reduction. The *visible* full scale auto-ranges: it snaps to the
// smallest of a few discrete endpoints (clamped to 5..15 dB) that still
// contains a slowly-decaying peak, so quiet material shows fine detail while
// heavy limiting still fits. A timer drives the peak decay and periodic redraw.
class HyraxMeterView : public VSTGUI::CControl
{
public:
    HyraxMeterView(const VSTGUI::CRect& size, int32_t tag);

    void draw(VSTGUI::CDrawContext* context) override;
    void setValue(float value) override;

    bool attached(VSTGUI::CView* parent) override;
    bool removed(VSTGUI::CView* parent) override;

    CLASS_METHODS(HyraxMeterView, VSTGUI::CControl)

private:
    // Current reduction in positive dB, from the bound parameter value.
    double currentDb() const;
    void onTimer();

    double peakHoldDb_ = 0.0;  // decaying recent peak, drives the auto-range
    double fullScaleDb_ = 5.0; // current visible full scale (5, 10 or 15 dB)
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace cotg::hyrax
