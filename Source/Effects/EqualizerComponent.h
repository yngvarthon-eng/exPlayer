#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Equalizer.h"
#include "EqPresetManager.h"

class AudioEngine;

class EqualizerComponent : public juce::Component
{
public:
    EqualizerComponent(AudioEngine* engine);
    ~EqualizerComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isExpanded() const { return expanded; }

    // Fired after the 30-band view is toggled, since that changes how much
    // width this component wants -- the parent should re-layout in response.
    std::function<void()> onExpandedChanged;

private:
    void refreshPresetCombo();
    void applyPreset(int comboIndex);
    void onSavePreset();
    void onDeletePreset();

    void onToggleExpanded();
    void syncVisualBandsFromReal();
    void onVisualBandChanged(int visualIndex);

    AudioEngine* audioEngine;
    EqPresetManager presetManager;

    std::unique_ptr<juce::ComboBox> presetCombo;
    std::unique_ptr<juce::TextButton> saveButton;
    std::unique_ptr<juce::TextButton> deleteButton;
    std::unique_ptr<juce::TextButton> expandButton;

    bool expanded = false;

    std::array<std::unique_ptr<juce::Slider>, 10> bandSliders;
    std::array<std::unique_ptr<juce::Label>, 10>  bandLabels;

    // Purely presentational 30-band view -- there are still only 10 real
    // filters in Equalizer, so each of these maps onto whichever real band
    // is closest in frequency (see visualToRealBand). Moving one moves every
    // other visual slider that shares its real band, so the view stays
    // consistent no matter which of the group you drag.
    static constexpr int numVisualBands = 30;
    std::array<std::unique_ptr<juce::Slider>, numVisualBands> visualBandSliders;
    std::array<std::unique_ptr<juce::Label>, numVisualBands>  visualBandLabels;
    std::array<int, numVisualBands> visualToRealBand;

    std::unique_ptr<juce::Slider> volumeSlider;
    std::unique_ptr<juce::Label>  volumeLabel;

    static constexpr const char* bandNames[10] = {
        "31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz",
        "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"
    };

    static constexpr float visualFrequencies[numVisualBands] = {
        20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f,
        200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f,
        2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f
    };

    static constexpr const char* visualBandNames[numVisualBands] = {
        "20", "25", "31", "40", "50", "63", "80", "100", "125", "160",
        "200", "250", "315", "400", "500", "630", "800", "1k", "1.2k", "1.6k",
        "2k", "2.5k", "3.1k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k"
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqualizerComponent)
};
