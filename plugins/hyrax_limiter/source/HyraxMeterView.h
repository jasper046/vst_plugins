#pragma once

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"

namespace Steinberg {
namespace Vst {
class EditController;
}
} // namespace Steinberg

namespace cotg::hyrax {

// Single-channel analog VU-style gain-reduction meter for the Hyrax editor.
// Renders one frame of a horizontal filmstrip bitmap (GainReductionMeter.png),
// picking the frame from the channel's read-only gain-reduction parameter. The
// view is instantiated once per channel (L / R) and placed side by side.
//
// The parameter is polled on a timer rather than bound to a control tag; the
// needle position is smoothed with a one-pole for realistic VU-like ballistics.
class HyraxMeterView : public VSTGUI::CView
{
public:
    HyraxMeterView(const VSTGUI::CRect& size, Steinberg::Vst::EditController* controller,
                   VSTGUI::CBitmap* filmstrip, int paramId);

    void draw(VSTGUI::CDrawContext* context) override;
    bool attached(VSTGUI::CView* parent) override;
    bool removed(VSTGUI::CView* parent) override;

    CLASS_METHODS(HyraxMeterView, VSTGUI::CView)

private:
    double readReductionDb() const; // current reduction (dB, positive) from the controller
    void onTimer();

    Steinberg::Vst::EditController* controller_ = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> filmstrip_;
    int paramId_ = 0;
    double displayedDb_ = 0.0; // smoothed needle position (dB)
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace cotg::hyrax
