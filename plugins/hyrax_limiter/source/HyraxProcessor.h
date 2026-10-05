#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

#include "HyraxDsp.h"
#include "HyraxParams.h"

namespace cotg::hyrax {

// VST3 audio processor for the Hyrax limiter. Owns the DSP engine, translates
// host parameter changes into engine parameters, and publishes meter values
// plus SENSE threshold updates back to the host.
class HyraxProcessor : public Steinberg::Vst::AudioEffect
{
public:
    HyraxProcessor();

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new HyraxProcessor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(
        Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
    void initDefaults();
    void applyParametersToEngine();

    // Reported processing latency in samples for the current parameters (look-
    // ahead + the always-on oversampled guard + the ferro stage when engaged).
    // Shared by getLatencySamples() and the latency-matched bypass so a
    // soft-bypassed signal stays sample-aligned with the host's delay
    // compensation (otherwise a dry/wet null combs instead of cancelling).
    int reportedLatencySamples() const;

    template <typename SampleT>
    void processChannels(SampleT** in, SampleT** out, int numChannels,
                         Steinberg::int32 numSamples);

    HyraxDsp dsp_;
    double sampleRate_ = 48000.0;
    double norm_[kNumAutomatable];
    bool paramsDirty_ = true;
    bool bypassed_ = false;

    // Latency-matched bypass delay (one per channel), kept warm every block so
    // toggling bypass is click-free. Sized to the maximum possible latency.
    cotg::dsp::RingBuffer bypassL_;
    cotg::dsp::RingBuffer bypassR_;

    // Per-channel peak gain reduction (positive dB) over the block currently
    // being processed; published to the editor via output parameter changes.
    double blockPeakGrDbL_ = 0.0;
    double blockPeakGrDbR_ = 0.0;
};

} // namespace cotg::hyrax
