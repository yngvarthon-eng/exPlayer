#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Visualization/ModulePatternData.h"

// The module's song message (credits, greetings, ASCII art) for the
// fullscreen sequencer's side panel. Shown in a monospaced font with the
// lines kept as written, sized so the longest line fits the width; a message
// taller than the section rolls slowly upwards like end credits, pausing at
// the top and bottom.
class SongMessageComponent : public juce::Component
{
public:
    explicit SongMessageComponent(const ModulePatternData& data);

    // Called every frame by the side panel; advances the roll.
    // Returns true when a new message arrived (the preferred height changed).
    bool tick();
    int getPreferredHeight(int width) const;

    void paint(juce::Graphics&) override;

private:
    bool rebuildIfDataChanged();
    float fontSizeFor(int width) const;

    const ModulePatternData& data;
    juce::String shownMessage { "<none>" };
    juce::StringArray lines;
    int longestLine = 0;

    double scroll = 0.0;          // pixels rolled
    double pauseUntilMs = 0.0;
    double lastTickMs = 0.0;

    static constexpr int padding = 8;
    static constexpr int maxPreferredLines = 24;
    static constexpr double scrollPixelsPerSecond = 14.0;
    static constexpr double pauseMs = 3000.0;
};
