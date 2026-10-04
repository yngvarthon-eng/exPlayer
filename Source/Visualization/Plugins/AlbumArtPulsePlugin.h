#pragma once

#include "../VisualizationPlugin.h"
#include <vector>

// A user-supplied picture that pulses in size to the bass, with a translucent
// waveform drawn over it. Unlike every other plugin here, this one needs an
// external asset (the image) -- VisualizationComponent owns picking the file
// and feeds it in via setImage(), both for the live tab and for a fresh
// export-thread instance (see VisualizationComponent::makePluginFactory()).
class AlbumArtPulsePlugin : public VisualizationPlugin
{
public:
    AlbumArtPulsePlugin() = default;

    juce::String getName() const override { return "Album Art Pulse"; }

    // Safe to call before update()/render() start running, and safe to call
    // with an already-valid image to replace it (e.g. the user picks another
    // picture). Never called concurrently with render() by any caller in this
    // codebase -- the live tab only reassigns it from the message thread, and
    // an export-thread instance has it set once before its own thread starts.
    void setImage(const juce::Image& newImage) { image = newImage; }

    // Where the picture's centre sits as a fraction of the frame's height: 0 =
    // top edge, 0.5 = the old always-centred default, 1 = bottom edge. Set from
    // VisualizationComponent's click-and-drag handling on the live tab, and
    // snapshotted into an export-thread instance the same way as the image.
    void setVerticalAnchor(float anchor) { verticalAnchor = juce::jlimit(0.0f, 1.0f, anchor); }

    // Colour of the waveform overlay stroke; white by default. Set from
    // VisualizationComponent's right-click colour menu, snapshotted the same
    // way as the image and vertical anchor.
    void setWaveformColour(juce::Colour colour) { waveformColour = colour; }

    void update(const juce::AudioBuffer<float>& buffer,
                const juce::Array<float>& spectrum) override;

    void render(juce::Graphics& g, juce::Rectangle<int> bounds) override;

private:
    juce::Image image;
    float verticalAnchor = 0.5f;
    juce::Colour waveformColour = juce::Colours::white;

    // Smoothed low-frequency envelope (0..1) driving the pulse scale; rises fast
    // on a bass hit and decays slowly, same shape as the bar plugins' envelopes.
    float bassEnvelope = 0.0f;
    static constexpr float kRiseRate  = 0.55f;
    static constexpr float kDecayRate = 0.045f;
    static constexpr float kPulseAmount = 0.16f; // max extra scale at full bass energy

    // How much of the spectrum counts as "bass": the lowest 1/kNumBassBands of the
    // log-spaced range, matching the log-bin split used by SpectrumBarsPlugin/
    // BarsPeakHoldPlugin (bin 0 is DC, so the band starts at bin 1).
    static constexpr float kNumBassBands = 12.0f;

    std::vector<float> waveformData;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlbumArtPulsePlugin)
};
