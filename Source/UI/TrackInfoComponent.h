#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"

// Track info card for the fullscreen sequencer's side panel: the song's title
// (the module's own, falling back to the playlist name), artist, file and
// format, the module's shape (channels, patterns, song length in order
// positions), where playback is (order / pattern / row), and elapsed and total
// time with a thin progress bar.
class TrackInfoComponent : public juce::Component
{
public:
    TrackInfoComponent(AudioEngine& engine, PlaylistManager& playlist);

    void update(const PatternPlayPosition& position);

    static constexpr int preferredHeight = 160;

    void paint(juce::Graphics&) override;

private:
    AudioEngine& audioEngine;
    PlaylistManager& playlistManager;
    PatternPlayPosition position;
    int lastSecond = -1;
};
