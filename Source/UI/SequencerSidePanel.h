#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"
#include "../Visualization/ModulePatternData.h"
#include "ReservesTopRightCorner.h"

class VisualizationComponent;
class SongOverviewComponent;
class ChannelActivityComponent;
class PianoRollComponent;
class TrackInfoComponent;
class InstrumentNamesComponent;
class SongMessageComponent;
class ChannelScopesComponent;

// The panel the fullscreen sequencer view puts beside the pattern grid, in the
// width the grid doesn't need (a 4-channel MOD leaves most of a 1920px screen
// empty). A toggle bar along the top switches each section on and off; the
// visible ones are stacked top to bottom. Sections that size to their content
// (song overview, channels, track info...) take what they ask for -- scaled
// down together when they'd take more than their share -- and the flexible ones (visualizer, piano roll) split whatever
// height is left.
//
// Adding a section: give it a component, a toggle button and an entry in
// `sections` (see the constructor) -- layout and toggling are generic.
//
// Polls the engine itself rather than being fed by MainComponent's timer, so
// it only exists, and only costs anything, while the fullscreen view is open.
class SequencerSidePanel : public juce::Component,
                           public ReservesTopRightCorner,
                           private juce::Timer
{
public:
    SequencerSidePanel(AudioEngine& engine, PlaylistManager& playlist);
    ~SequencerSidePanel() override;

    // Which sections are switched on, as a bitmask over Section. Kept by the
    // owner across openings of the fullscreen view (the panel itself is
    // rebuilt each time).
    enum Section
    {
        visualizerSection   = 1 << 0,
        songOverviewSection = 1 << 1,
        channelActivitySection = 1 << 2,
        pianoRollSection       = 1 << 3,
        trackInfoSection       = 1 << 4,
        instrumentNamesSection = 1 << 5,
        songMessageSection     = 1 << 6,
        channelScopesSection   = 1 << 7,
    };
    static constexpr int defaultSections = visualizerSection | songOverviewSection;

    void setVisibleSections(int mask);
    int  getVisibleSections() const;
    std::function<void(int mask)> onVisibleSectionsChanged;

    // Forwards '1'-'9' to the visualizer's tab hotkeys while it is shown.
    bool handleKeyPress(const juce::KeyPress& key);

    void setTopRightReserve(int width) override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct SectionEntry
    {
        Section section;
        juce::Component* component;
        std::unique_ptr<juce::TextButton> toggle;
        bool flexible;                        // shares leftover height
        std::function<int(int width)> preferredHeight; // non-flexible only
    };

    AudioEngine& audioEngine;
    std::unique_ptr<VisualizationComponent> visualizer;
    std::unique_ptr<SongOverviewComponent>  songOverview;
    std::unique_ptr<ChannelActivityComponent> channelActivity;
    std::unique_ptr<PianoRollComponent> pianoRoll;
    std::unique_ptr<TrackInfoComponent> trackInfo;
    std::unique_ptr<InstrumentNamesComponent> instrumentNames;
    std::unique_ptr<SongMessageComponent> songMessage;
    std::unique_ptr<ChannelScopesComponent> channelScopes;
    std::vector<float> channelLevels;
    int toggleBarBottom = 0;
    int topRightReserve = 0;
    std::vector<SectionEntry> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerSidePanel)
};
