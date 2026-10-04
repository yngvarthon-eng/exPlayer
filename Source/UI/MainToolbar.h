#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioQualitySelector.h"

class AudioEngine;

// The strip of view controls between the transport bar and the panels: one
// toggle per collapsible panel, the three fullscreen launchers, and the output
// quality selectors pinned to the right hand end.
//
// A collapse toggle only records state here -- what that state means for the
// panels (which component is visible, and therefore how the rest of the window
// is laid out) is the owner's business, so the toolbar reports the change
// through onLayoutChanged() instead of touching its siblings directly.
class MainToolbar : public juce::Component
{
public:
    explicit MainToolbar(AudioEngine& engine);

    // A collapse toggle was switched: the owner should apply the new
    // visibility and re-lay out its panels.
    std::function<void()> onLayoutChanged;
    std::function<void()> onOpenFullscreenVisualizer;
    std::function<void()> onOpenFullscreenPatternViewer;
    std::function<void()> onOpenFullscreenVideo;

    // The fullscreen sequencer view is only meaningful once a module is loaded.
    void setPatternFullscreenEnabled(bool shouldEnable);

    // Likewise, the fullscreen video view has nothing to show until a video is
    // loaded.
    void setVideoFullscreenEnabled(bool shouldEnable);

    // Re-reads the output device's advertised sample rates. Call once the audio
    // device is open.
    void refreshAudioQuality();

    bool isPlaylistCollapsed() const;
    bool isEqCollapsed() const;
    bool isVizCollapsed() const;
    bool isFxCollapsed() const;
    bool isAnalysisCollapsed() const;

    void resized() override;

    static constexpr int preferredHeight = 28;
    static constexpr int sideMargin     = 4;
    static constexpr int verticalMargin = 2;
    static constexpr int buttonGap      = 4;

private:
    // Places the next button against the left edge of the shrinking strip,
    // leaving the standard gap after it.
    void placeButton(juce::Rectangle<int>& strip, juce::Component& button, int width);

    juce::TextButton collapsePlaylistBtn { "Playlist" };
    juce::TextButton collapseEqBtn       { "Equalizer" };
    juce::TextButton collapseVizBtn      { "Visualizer" };
    juce::TextButton fullscreenVizBtn    { "Fullscreen" };
    juce::TextButton fullscreenPatternBtn { "Sequencer FS" };
    juce::TextButton fullscreenVideoBtn  { "Video FS" };
    juce::TextButton collapseFxBtn       { "FX Chain" };
    juce::TextButton collapseAnalysisBtn { "Analysis" };

    AudioQualitySelector audioQuality;

    static constexpr int playlistButtonWidth   = 90;
    static constexpr int eqButtonWidth         = 90;
    static constexpr int vizButtonWidth        = 90;
    static constexpr int fullscreenButtonWidth = 90;
    static constexpr int patternButtonWidth    = 100;
    static constexpr int videoButtonWidth      = 80;
    static constexpr int fxButtonWidth         = 80;
    static constexpr int analysisButtonWidth   = 80;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainToolbar)
};
