#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Visualization/SpectrumAnalyzer.h"
#include "../Visualization/VisualizationPlugin.h"

class VisualizationComponent : public juce::Component,
                               public juce::Timer
{
public:
    explicit VisualizationComponent(AudioEngine* engine);
    ~VisualizationComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Accessors used by VideoExporter to mirror "whatever tab is currently shown".
    int getActivePluginIndex() const { return activePlugin; }
    int getNumPlugins() const { return plugins.size(); }
    juce::String getPluginName(int index) const;

    // Used by the fullscreen visualizer window to open on the same tab as the
    // embedded view it mirrors.
    void setActivePluginIndex(int index);

    // Digit-key shortcut: '1'-'9' selects that tab directly, as an alternative
    // to clicking it. Returns true if `key` was a digit that selected a tab.
    bool handleDigitHotkey(const juce::KeyPress& key);

    // Fresh instance of the plugin at `index` (same type as the live tab), for use
    // on the export thread -- never the live UI's own plugin instance.
    std::unique_ptr<VisualizationPlugin> createPluginInstance(int index) const;

    // Same as createPluginInstance(index), but also snapshots the current album
    // art image right now (on the message thread) and bakes it into the factory,
    // so the Album Art Pulse tab's picture reaches an export-thread instance
    // without that thread ever reading this component's own state later -- same
    // cross-thread-safety reasoning as the EQ gains snapshot taken alongside it
    // at every export call site.
    std::function<std::unique_ptr<VisualizationPlugin>()> makePluginFactory(int index) const;

private:
    AudioEngine* audioEngine;

    SpectrumAnalyzer spectrumAnalyzer;
    juce::OwnedArray<VisualizationPlugin> plugins;
    int activePlugin = 0;

    juce::Image albumArtImage;
    float albumArtVerticalAnchor = 0.5f; // 0 = top edge, 0.5 = centred, 1 = bottom edge
    juce::Colour albumArtWaveformColour = juce::Colours::white;
    std::unique_ptr<juce::FileChooser> albumArtFileChooser;
    void chooseAlbumArtImage();
    void chooseAlbumArtWaveformColour();
    void setAlbumArtImage(const juce::Image& newImage);
    void setAlbumArtVerticalAnchor(float anchor);
    void setAlbumArtWaveformColour(juce::Colour colour);

    // Click-and-drag state for repositioning the Album Art Pulse picture -- a
    // drag that barely moved is treated as a plain click (replace the picture)
    // once mouseUp sees how far it actually travelled.
    bool draggingAlbumArt = false;
    int albumArtDragStartY = 0;
    float albumArtDragStartAnchor = 0.5f;

    static constexpr int kTabH = 22; // height of the tab bar

    void drawTabs(juce::Graphics& g) const;
    juce::Rectangle<int> pluginArea() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizationComponent)
};
