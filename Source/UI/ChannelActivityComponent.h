#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Visualization/ModulePatternData.h"

// One column per module channel that lights up when a note triggers on it and
// fades out afterwards -- a per-channel "who is playing now" strip for the
// fullscreen sequencer's side panel.
//
// Driven from the pattern data, not from the audio: each row playback crosses
// is read back from ModulePatternData, so the flashes stay in step with the
// grid beside it. A note flashes the column in its instrument's colour, at the
// cell's volume-column level when it has one (vXX, 0..64); a note cut or
// key-off drops it quickly. Rows crossed between two polls (fast tempos) are
// all replayed, so no trigger is missed.
class ChannelActivityComponent : public juce::Component
{
public:
    explicit ChannelActivityComponent(const ModulePatternData& data);

    // Called from the side panel's timer with the current playback position.
    void update(const PatternPlayPosition& position, bool isPlaying);

    static constexpr int preferredHeight = 170;

    void paint(juce::Graphics&) override;

private:
    struct Channel
    {
        float level      = 0.0f;   // 0..1, decays over time
        float decayRate  = 3.0f;   // per second (exponential)
        juce::Colour colour;
        juce::String note;         // last note triggered, for the label
    };

    void processRow(const ModulePattern& pattern, int row);
    void ensureChannelCount();

    const ModulePatternData& data;
    std::vector<Channel> channels;

    int lastPattern = -1;
    int lastRow     = -1;
    int lastOrder   = -1;
    double lastTickMs = 0.0;
};
