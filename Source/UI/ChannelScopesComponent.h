#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>

// Per-channel level scopes for the fullscreen sequencer's side panel: one
// tile per module channel showing that channel's output level over the last
// few seconds as a mirrored, scrolling envelope, newest on the right.
//
// These are real audio levels -- libopenmpt's per-channel VU, read from the
// playing decoder (AudioEngine::takeModuleChannelLevels) -- not a waveform:
// libopenmpt mixes all channels together and has no per-channel sample output.
class ChannelScopesComponent : public juce::Component
{
public:
    ChannelScopesComponent();

    // `levels` holds one peak level per channel since the last call (empty
    // when the track can't report them).
    // Returns true when the channel count changed (preferred height changed).
    bool update(const std::vector<float>& levels);
    int getPreferredHeight(int width) const;

    void paint(juce::Graphics&) override;

private:
    juce::Point<int> gridFor(int width) const; // columns, rows

    static constexpr int historyLength = 96;   // ~3 s at the panel's 30 Hz
    static constexpr int minTileW = 84, tileH = 46, minTileH = 16, padding = 6, gap = 3;

    std::vector<std::array<float, historyLength>> history;
    int writePos = 0;
    bool available = false;
};
