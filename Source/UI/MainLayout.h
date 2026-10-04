#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Window geometry and panel-visibility policy for MainComponent, decided from
// plain flags.
//
// MainComponent used to hold both the decisions ("the visualizer is hidden
// while a video is playing") and the arithmetic that turns them into
// rectangles, mixed in with the component wiring. Separating the two leaves
// MainComponent as wiring only, and lets the layout be reasoned about -- and
// tested -- with no live AudioEngine, toolbar or component tree: the owner
// gathers the flags, this decides what is visible and where, and the owner
// applies the result to its panels.
//
// Two rules run through the whole thing:
//   * Every panel is either visible and given a slot, or hidden and given an
//     empty rectangle; an empty rectangle means "no slot in this
//     configuration", not "off screen".
//   * The window is filled top to bottom in a fixed order, so collapsing a
//     panel hands its pixels to the panels below rather than leaving a gap.
namespace MainLayout
{
    // Which panels the toolbar's collapse toggles and the currently loaded
    // track leave open. Everything the layout depends on is in here, so the
    // same state always produces the same window.
    struct PanelState
    {
        bool playlistCollapsed    = false;
        bool eqCollapsed          = false;
        bool vizCollapsed         = false;
        bool fxCollapsed          = false;
        bool analysisCollapsed    = false;

        // The 30-band equalizer view needs far more width than the 10-band one.
        bool equalizerExpanded    = false;

        // A tracker module is loaded, so the pattern viewer has something to
        // draw and takes a share of the space above the visualizer.
        bool patternViewerVisible = false;

        // The current track is a video; video and the visualizer share one
        // panel slot and are mutually exclusive.
        bool videoVisible         = false;
    };

    // What the caller should show and hide. Kept separate from Areas so a
    // toggle can change visibility without the caller re-deriving the slots.
    //
    // The pattern viewer is deliberately absent: its visibility follows the
    // loaded track rather than the toggles (a module is playing or it is not),
    // so it stays driven from one place -- the owner's track handling -- and
    // PanelState::patternViewerVisible only feeds the geometry, read back from
    // the component itself.
    struct Visibility
    {
        bool playlistEditor = false;
        bool equalizer      = false;
        bool analysis       = false;
        bool fxChain        = false;
        bool midiKeyboard   = false;
        bool midiButton     = false;
        bool visualization  = false;
        bool videoPreview   = false;
    };

    // Where each panel goes for a window of the given size. Panels with no slot
    // in this configuration get an empty rectangle.
    struct Areas
    {
        juce::Rectangle<int> playerControls;
        juce::Rectangle<int> toolbar;
        juce::Rectangle<int> analysis;
        juce::Rectangle<int> patternViewer;

        // Video and the visualizer share this slot, so both rectangles are the
        // same: the caller shows whichever one is current and leaves the other
        // hidden.
        juce::Rectangle<int> visualization;
        juce::Rectangle<int> videoPreview;

        juce::Rectangle<int> pluginChain;
        juce::Rectangle<int> midiButton;
        juce::Rectangle<int> midiKeyboard;

        // The bottom row: the playlist takes whatever the equalizer does not.
        juce::Rectangle<int> playlist;
        juce::Rectangle<int> equalizer;
    };

    // Fixed panel sizes, in pixels. The toolbar's own height lives on
    // MainToolbar, which also owns how it divides up its strip.
    namespace heights
    {
        constexpr int transportBar  = 130;
        constexpr int analysisPanel = 52;
        constexpr int fxPanel       = 146; // plugin list + button row + preset row
        constexpr int keyboardStrip = 44;  // on-screen MIDI keyboard for the chain
        constexpr int midiButton    = 70;

        // Bottom row (playlist + equalizer): the equalizer needs a minimum
        // height's worth of room to be usable, the playlist more.
        constexpr int playlistAndEq = 340;
        constexpr int eqOnly        = 240;
        constexpr int playlistOnly  = 280;

        // The 30-band view needs much more width than the 10-band one to stay
        // legible, so it eats into the playlist panel's space.
        constexpr int expandedEq = 760;
        constexpr int compactEq  = 340;

        // Pattern viewer and visualizer share what's left; neither is ever
        // squeezed below this, even when every panel is open in a short window.
        constexpr int minimumPanel    = 100;
        constexpr int vizAbovePattern = 100;
    }

    // Which panels are visible for the given state. The FX panel carries the
    // on-screen keyboard, so hiding the chain hides the keyboard and the MIDI
    // input button with it; video wins over the visualizer in the shared slot.
    Visibility computeVisibility (const PanelState& state);

    // Where every panel goes inside a window of the given area (normally
    // Component::getLocalBounds()). Rectangles for collapsed or absent panels
    // come back empty.
    Areas computeAreas (const juce::Rectangle<int>& windowArea, const PanelState& state);
}
