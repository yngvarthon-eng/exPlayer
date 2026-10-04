#include "AudioQualitySelector.h"
#include "../Audio/AudioEngine.h"
#include <cmath>

namespace
{
    // Whole-kHz rates read better as integers (48 kHz); fractional ones need
    // the decimal (44.1 kHz).
    juce::String formatSampleRate(double rate)
    {
        if (std::fmod(rate, 1000.0) == 0.0)
            return juce::String((int) (rate / 1000.0)) + " kHz";

        return juce::String(rate / 1000.0, 1) + " kHz";
    }

    int bitDepthForItemId(int itemId)
    {
        switch (itemId)
        {
            case 1:  return 16;
            case 2:  return 24;
            default: return 32;
        }
    }
}

AudioQualitySelector::AudioQualitySelector(AudioEngine& engine)
    : audioEngine(engine)
{
    sampleRateBox.onChange = [this]
    {
        const int id = sampleRateBox.getSelectedId();

        if (juce::isPositiveAndBelow(id - 1, availableSampleRates.size()))
        {
            // setSampleRate() validates against the hardware and reverts on
            // failure; either way, re-sync the box to the rate the device
            // actually ended up using.
            audioEngine.setSampleRate(availableSampleRates[id - 1]);
            updateSampleRateSelection();
        }
    };
    addAndMakeVisible(sampleRateBox);

    bitDepthBox.addItem("16-bit", 1);
    bitDepthBox.addItem("24-bit", 2);
    bitDepthBox.addItem("32-bit", 3);
    bitDepthBox.setSelectedId(1);
    bitDepthBox.onChange = [this]
    {
        audioEngine.setBitDepth(bitDepthForItemId(bitDepthBox.getSelectedId()));
    };
    addAndMakeVisible(bitDepthBox);
}

void AudioQualitySelector::refreshSampleRates()
{
    availableSampleRates = audioEngine.getSupportedSampleRates();

    sampleRateBox.clear(juce::dontSendNotification);

    for (int i = 0; i < availableSampleRates.size(); ++i)
        sampleRateBox.addItem(formatSampleRate(availableSampleRates[i]), i + 1);

    // No device / no advertised rates: nothing useful to choose.
    sampleRateBox.setEnabled(! availableSampleRates.isEmpty());

    updateSampleRateSelection();
}

void AudioQualitySelector::updateSampleRateSelection()
{
    const double current = audioEngine.getCurrentSampleRate();

    for (int i = 0; i < availableSampleRates.size(); ++i)
    {
        if (std::abs(availableSampleRates[i] - current) < 1.0)
        {
            sampleRateBox.setSelectedId(i + 1, juce::dontSendNotification);
            return;
        }
    }
}

void AudioQualitySelector::resized()
{
    auto area = getLocalBounds();

    bitDepthBox.setBounds(area.removeFromRight(bitDepthBoxWidth));
    area.removeFromRight(boxGap);
    sampleRateBox.setBounds(area.removeFromRight(sampleRateBoxWidth));
}
