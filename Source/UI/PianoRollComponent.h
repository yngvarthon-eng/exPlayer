#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Visualization/ModulePatternData.h"

// The module's notes as a scrolling piano roll for the fullscreen sequencer's
// side panel: time runs left to right, pitch bottom to top, with a keyboard
// along the left edge whose keys light up while their notes sound.
//
// The view follows the song order, not just the current pattern, so the
// patterns before and after flow on from it. The play line sits a quarter of
// the way in, leaving most of the width for what's coming. A note lasts until
// the next note, cut or key-off on its channel; each one is coloured by its
// instrument, as in the Channels section.
class PianoRollComponent : public juce::Component
{
public:
    explicit PianoRollComponent(const ModulePatternData& data);

    void update(const PatternPlayPosition& position);

    void paint(juce::Graphics&) override;

    static constexpr int rowsBehind = 16;  // shown left of the play line
    static constexpr int rowsAhead  = 48;  // and right of it

private:
    // Each pattern cell reduced to what the roll needs.
    struct Cell { short pitch = -1; short instrument = 0; bool stop = false; };
    struct PatternCells { int numRows = 0, numChannels = 0; std::vector<Cell> cells; };

    // One row of the song timeline: which pattern row plays there.
    struct TimelineRow { int pattern; int row; };

    void rebuildIfDataChanged();
    const PatternCells* cellsFor(int pattern);

    // Song-order rows from `before` rows before the current one to `after`
    // rows after it (fewer at the song's ends). Sets currentIndex to the
    // current row's index in the result.
    std::vector<TimelineRow> buildTimeline(int before, int after, int& currentIndex) const;

    const ModulePatternData& data;
    juce::String dataSignature { "<none>" };
    std::vector<PatternCells> patternCells;   // by pattern index, filled lazily
    std::vector<bool> patternCellsReady;
    std::vector<int> playableOrder;           // order entries that name a pattern
    int lowestPitch = 48, highestPitch = 72;

    PatternPlayPosition position;
};
