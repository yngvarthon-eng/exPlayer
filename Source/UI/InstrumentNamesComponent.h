#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Visualization/ModulePatternData.h"

// The module's instrument (or sample) names for the fullscreen sequencer's
// side panel -- where tracker musicians traditionally hid their greetings,
// credits and ASCII art. Listed in as many columns as fit, numbered as the
// pattern grid numbers them; each one lights up in its instrument colour (the
// Channels and Piano roll colours) when a note plays it.
class InstrumentNamesComponent : public juce::Component
{
public:
    explicit InstrumentNamesComponent(const ModulePatternData& data);

    // Returns true when another module's names arrived (preferred height changed).
    bool update(const PatternPlayPosition& position, bool isPlaying);
    int getPreferredHeight(int width) const;

    void paint(juce::Graphics&) override;

private:
    bool rebuildIfDataChanged();
    void trigger(const ModulePattern& pattern, int row);
    int columnsFor(int width, int height) const;

    const ModulePatternData& data;
    juce::String dataSignature { "<none>" };
    std::map<int, float> glow;   // instrument number -> 0..1, decaying
    int lastPattern = -1, lastRow = -1, lastOrder = -1;
    double lastTickMs = 0.0;

    static constexpr int lineH = 15, minColumnW = 170, padding = 6;
};
