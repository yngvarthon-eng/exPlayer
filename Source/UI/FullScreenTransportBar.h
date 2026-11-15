#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"

// Compact transport strip for the fullscreen sequencer view, where the main
// window's player controls are hidden behind the kiosk window:
//
//   |<  <Pat  <<10  >/||  []  10>>  Pat>  >|   0:42 [=====o-------] 3:15   Track name
//
// previous track, previous pattern, back 10 s, play/pause, stop, forward
// 10 s, next pattern, next track, a seek bar with elapsed/total time, and the
// playing track's name. The pattern skips step through the song order (only
// for modules libopenmpt can time; .xtp songs and other tracks disable them). Polls the
// engine for its state, so it needs no hookup beyond the two pointers.
class FullScreenTransportBar : public juce::Component,
                               private juce::Timer
{
public:
    FullScreenTransportBar(AudioEngine& engine, PlaylistManager& playlist);
    ~FullScreenTransportBar() override;

    static constexpr int preferredHeight = 44;
    static constexpr double skipSeconds  = 10.0;

    // Jumps to the start of the next (+1) or previous (-1) order position.
    // Going back from more than a few rows into a pattern restarts it first,
    // like a CD player's previous-track button. Also on Page Up / Page Down.
    void skipPattern(int direction);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void skipBy(double seconds);
    void updatePlayPauseShape();

    AudioEngine& audioEngine;
    PlaylistManager& playlistManager;

    std::unique_ptr<juce::ShapeButton> prevTrackButton, backButton, playPauseButton,
                                       stopButton, forwardButton, nextTrackButton;
    std::unique_ptr<juce::TextButton> prevPatternButton, nextPatternButton;
    juce::Slider positionSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label elapsedLabel, totalLabel, titleLabel;

    // Nothing else in the app creates a TooltipWindow, so without this the
    // buttons' hints never show. A desktop-level one (no parent) so a tooltip
    // isn't clipped to this 44px strip.
    juce::TooltipWindow tooltipWindow { nullptr, 600 };

    bool draggingSlider = false;
    bool showingPause   = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FullScreenTransportBar)
};
