#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;

// The output-quality pickers (sample rate + bit depth) that sit at the right
// hand end of the toolbar.
//
// The component owns both combo boxes and keeps them in step with the engine:
// the rate list is whatever the open output device advertises, and a change the
// device refuses (AudioEngine::setSampleRate() restores the previous rate when
// that happens) simply re-selects the rate the device actually ended up on, so
// the box never disagrees with the audio.
class AudioQualitySelector : public juce::Component
{
public:
    explicit AudioQualitySelector(AudioEngine& engine);

    // Re-reads the rates the output device advertises, and selects the one in
    // use. Call once the audio device is open; until then the list is empty and
    // the box is disabled.
    void refreshSampleRates();

    void resized() override;

    static constexpr int sampleRateBoxWidth = 90;
    static constexpr int bitDepthBoxWidth   = 80;
    static constexpr int boxGap             = 5;
    static constexpr int preferredWidth     = sampleRateBoxWidth + boxGap + bitDepthBoxWidth;

private:
    // Selects the entry matching the engine's actual current rate, without
    // firing the change callback.
    void updateSampleRateSelection();

    AudioEngine& audioEngine;

    juce::ComboBox sampleRateBox;
    juce::ComboBox bitDepthBox;

    // Rates indexed by (item id - 1): combo box ids are 1-based.
    juce::Array<double> availableSampleRates;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioQualitySelector)
};
