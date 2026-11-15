#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Visualization/ModulePatternData.h"
#include "ReservesTopRightCorner.h"

class PatternGridComponent;

class ModulePatternViewer : public juce::Component,
                            public ReservesTopRightCorner
{
public:
    ModulePatternViewer();
    ~ModulePatternViewer() override;

    void setPatternData(const ModulePatternData* data);
    void setCurrentRow(int row);

    // When enabled, the grid shrinks each channel column so every channel in
    // the pattern fits within the viewport width instead of being clipped by
    // it -- there's no scrollbar in a baked video frame, so used by
    // VideoExporter's PatternGrid/Combined modes. Off by default, which keeps
    // the interactive viewer's fixed-width, horizontally-scrollable columns.
    void setFitChannelsToWidth(bool shouldFit);

    // Centered view (the default, as in exTracker's "View: Center"): a fixed
    // line in the middle of the grid holds the playing row and the pattern
    // scrolls smoothly under it, with the neighbouring patterns of the song
    // order dimmed above and below. Off ("View: Page"): the whole pattern is
    // laid out and the viewport scrolls to keep the playing row visible.
    void setCenteredView(bool shouldCenter);
    bool isCenteredView() const { return centeredView; }

    // Live playback, called from the UI timer: switches pattern to follow
    // playback and glides the centered view between the polled positions.
    void setPlaybackPosition(const PatternPlayPosition& position, bool isPlaying);

    // Offline rendering (video export): shows exactly `position`, with no
    // smoothing, as if playing.
    void setExactPlaybackPosition(const PatternPlayPosition& position);

    // Whole-row position only (no glide).
    void setPlaybackPosition(int patternIndex, int row);

    // Leaves room at the right end of the toolbar for the fullscreen window's
    // Exit button.
    void setTopRightReserve(int width) override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    int topRightReserve = 0;

    void showPattern(int index);
    void updateNavButtons();
    void updateViewButton();
    void refitGrid();
    void updateGhostPatterns(int orderPosition);
    void animateFrame();

    const ModulePatternData* patternData = nullptr;
    int currentPatternIndex = 0;
    bool fitChannelsToWidth = false;
    bool centeredView = true;

    // Live glide state (see animateFrame()).
    bool   playing        = false;
    double measuredRow    = 0.0;   // row + fraction from the last poll
    double measuredAtMs   = 0.0;
    double rowSeconds     = 0.0;
    double lastFrameMs    = 0.0;
    bool   gliding        = false;

    std::unique_ptr<juce::Label>      titleLabel;
    std::unique_ptr<juce::Label>      patternLabel;
    std::unique_ptr<juce::TextButton> viewButton;
    std::unique_ptr<juce::TextButton> prevButton;
    std::unique_ptr<juce::TextButton> nextButton;
    std::unique_ptr<juce::Label>      statusLabel;
    std::unique_ptr<juce::Viewport>   viewport;
    std::unique_ptr<PatternGridComponent> grid;
    std::unique_ptr<juce::VBlankAttachment> vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulePatternViewer)
};
