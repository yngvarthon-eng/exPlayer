#include "MainLayout.h"
#include "MainToolbar.h"

namespace MainLayout
{
    namespace
    {
        // Height of the bottom row (playlist + equalizer): zero when both
        // panels are collapsed, the equalizer's own minimum when it is alone,
        // and the playlist's when the equalizer is collapsed.
        int bottomRowHeight (const PanelState& state)
        {
            if (! state.playlistCollapsed && ! state.eqCollapsed) return heights::playlistAndEq;
            if (  state.playlistCollapsed && ! state.eqCollapsed) return heights::eqOnly;
            if (! state.playlistCollapsed &&   state.eqCollapsed) return heights::playlistOnly;
            return 0;
        }

        // Pixels left for the visualizer and the pattern viewer once every
        // fixed-height panel and the bottom row have taken their share. Never
        // less than the minimum, so a short window squeezes the flexible panels
        // rather than dropping one of them entirely.
        int flexiblePanelSpace (const PanelState& state, int windowHeight, int bottomHeight)
        {
            const int fixedHeight = heights::transportBar
                                  + MainToolbar::preferredHeight
                                  + (state.analysisCollapsed ? 0 : heights::analysisPanel)
                                  + (state.fxCollapsed       ? 0 : heights::fxPanel)
                                  + (state.fxCollapsed       ? 0 : heights::keyboardStrip)
                                  + bottomHeight;

            return juce::jmax (windowHeight - fixedHeight, heights::minimumPanel);
        }
    }

    Visibility computeVisibility (const PanelState& state)
    {
        Visibility visible;

        // The FX panel is the chain list plus the on-screen keyboard for playing
        // any synth loaded into it; the keyboard and its MIDI-input button are
        // only useful with the chain in view.
        visible.fxChain      = ! state.fxCollapsed;
        visible.midiKeyboard = visible.fxChain;
        visible.midiButton   = visible.fxChain;

        visible.playlistEditor = ! state.playlistCollapsed;
        visible.equalizer      = ! state.eqCollapsed;
        visible.analysis       = ! state.analysisCollapsed;

        visible.videoPreview  = state.videoVisible;
        // Video and the visualizer share one panel slot and are mutually
        // exclusive: never re-show the visualizer over an active video.
        visible.visualization = ! state.vizCollapsed && ! state.videoVisible;

        return visible;
    }

    Areas computeAreas (const juce::Rectangle<int>& windowArea, const PanelState& state)
    {
        Areas areas;
        auto area = windowArea;

        const int analysisH = state.analysisCollapsed ? 0 : heights::analysisPanel;
        const int fxH       = state.fxCollapsed       ? 0 : heights::fxPanel;
        const int keyboardH = state.fxCollapsed       ? 0 : heights::keyboardStrip;

        const int bottomH   = bottomRowHeight (state);
        const int remaining = flexiblePanelSpace (state, windowArea.getHeight(), bottomH);

        int vizH     = 0;
        int patternH = 0;

        if (state.patternViewerVisible)
        {
            vizH     = state.vizCollapsed ? 0 : heights::vizAbovePattern;
            patternH = juce::jmax (heights::minimumPanel, remaining - vizH);
        }
        else
        {
            vizH = state.vizCollapsed ? 0 : remaining;
        }

        areas.playerControls = area.removeFromTop (heights::transportBar);

        // The toolbar's own vertical margins live inside MainToolbar::resized().
        areas.toolbar = area.removeFromTop (MainToolbar::preferredHeight);

        if (analysisH > 0)
            areas.analysis = area.removeFromTop (analysisH);

        if (patternH > 0)
            areas.patternViewer = area.removeFromTop (patternH);

        // Video and the visualizer share this slot; the caller shows whichever
        // of the two is current and leaves the other hidden.
        areas.visualization = area.removeFromTop (vizH);
        areas.videoPreview  = areas.visualization;

        if (fxH > 0)
            areas.pluginChain = area.removeFromTop (fxH);

        if (keyboardH > 0)
        {
            auto keyboardArea  = area.removeFromTop (keyboardH);
            areas.midiButton   = keyboardArea.removeFromLeft (heights::midiButton);
            areas.midiKeyboard = keyboardArea;
        }

        if (bottomH > 0)
        {
            if (! state.playlistCollapsed && ! state.eqCollapsed)
            {
                const int eqWidth = state.equalizerExpanded ? heights::expandedEq
                                                            : heights::compactEq;
                areas.equalizer = area.removeFromRight (eqWidth);
                areas.playlist  = area;
            }
            else if (! state.playlistCollapsed)
            {
                areas.playlist = area;
            }
            else
            {
                areas.equalizer = area;
            }
        }

        return areas;
    }
}
