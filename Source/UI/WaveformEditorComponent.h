#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Recorder/MicClip.h"

// Scrollable, zoomable waveform view of a MicClip with a time ruler, an edit
// cursor, a selection and a playhead.
//
//   click            set the cursor (clears the selection)
//   drag             select; shift-click extends the selection
//   double-click     select all
//   wheel            scroll; ctrl/cmd + wheel zooms around the mouse
//
// It only reads the clip; edits are applied by the owner, which calls
// clipChanged() afterwards so the view can keep cursor/selection in range.
class WaveformEditorComponent : public juce::Component,
                                private juce::ScrollBar::Listener
{
public:
    explicit WaveformEditorComponent(const MicClip& clipToShow);
    ~WaveformEditorComponent() override;

    using SampleRange = MicClip::SampleRange;

    SampleRange getSelection() const    { return selection; }
    void setSelection(SampleRange range);
    juce::int64 getCursor() const       { return cursor; }
    void setCursor(juce::int64 sample);
    void selectAll();

    // Shown as a vertical line; negative hides it.
    void setPlayhead(double sample);

    // While true, the view keeps the whole clip in sight as it grows (with a
    // minimum span so the first seconds of a take aren't stretched edge to edge).
    void setFollowRecording(bool shouldFollow);

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToSelection();

    // Call after the clip was edited or grew.
    void clipChanged();

    std::function<void()> onSelectionChanged;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;

    juce::Rectangle<int> getWaveArea() const;
    double sampleToX(double sample) const;
    juce::int64 xToSample(float x) const;
    juce::int64 visibleSamples() const;
    void setView(double newStart, double newSamplesPerPixel);
    void zoomAround(double factor, float anchorX);
    void clampView();
    void updateScrollBar();
    void notifySelection();

    void drawRuler(juce::Graphics&, juce::Rectangle<int> area) const;
    void drawChannel(juce::Graphics&, int channel, juce::Rectangle<int> area) const;

    const MicClip& clip;
    juce::ScrollBar scrollBar { false };

    double viewStart = 0.0;          // first visible sample
    double samplesPerPixel = 100.0;
    bool followRecording = false;

    juce::int64 cursor = 0;
    SampleRange selection;
    juce::int64 dragAnchor = 0;
    double playhead = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformEditorComponent)
};
