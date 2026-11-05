#include "WaveformEditorComponent.h"
#include <cmath>

namespace
{
    constexpr int rulerHeight = 20;
    constexpr int scrollBarHeight = 12;
    constexpr double minSamplesPerPixel = 0.125; // 8 px per sample at full zoom
    constexpr double minFollowSeconds = 10.0;

    const juce::Colour backgroundColour  { 0xff1b1d21 };
    const juce::Colour rulerColour       { 0xff2a2d33 };
    const juce::Colour waveColour        { 0xff4fc3f7 };
    const juce::Colour selectionColour   { 0x40ffffff };
    const juce::Colour cursorColour      { 0xffffd54f };
    const juce::Colour playheadColour    { 0xff66bb6a };
    const juce::Colour centreLineColour  { 0xff3a3e46 };

    juce::String formatRulerTime(double seconds, double step)
    {
        const int minutes = (int) (seconds / 60.0);
        const double rest = seconds - minutes * 60.0;

        if (step < 1.0)
        {
            const int decimals = step < 0.01 ? 3 : (step < 0.1 ? 2 : 1);
            return minutes > 0 ? juce::String(minutes) + ":" + juce::String(rest, decimals).paddedLeft('0', decimals + 3)
                               : juce::String(rest, decimals);
        }

        return juce::String(minutes) + ":" + juce::String((int) std::round(rest)).paddedLeft('0', 2);
    }
}

WaveformEditorComponent::WaveformEditorComponent(const MicClip& clipToShow)
    : clip(clipToShow)
{
    scrollBar.setAutoHide(false);
    scrollBar.addListener(this);
    addAndMakeVisible(scrollBar);
    setWantsKeyboardFocus(false); // the recorder window handles the keys
}

WaveformEditorComponent::~WaveformEditorComponent()
{
    scrollBar.removeListener(this);
}

// ---- Geometry ----

juce::Rectangle<int> WaveformEditorComponent::getWaveArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(rulerHeight);
    area.removeFromBottom(scrollBarHeight);
    return area;
}

double WaveformEditorComponent::sampleToX(double sample) const
{
    return (double) getWaveArea().getX() + (sample - viewStart) / samplesPerPixel;
}

juce::int64 WaveformEditorComponent::xToSample(float x) const
{
    const double sample = viewStart + ((double) x - getWaveArea().getX()) * samplesPerPixel;
    return juce::jlimit((juce::int64) 0, clip.getNumSamples(), (juce::int64) std::llround(sample));
}

juce::int64 WaveformEditorComponent::visibleSamples() const
{
    return (juce::int64) std::ceil(getWaveArea().getWidth() * samplesPerPixel);
}

void WaveformEditorComponent::clampView()
{
    const int width = juce::jmax(1, getWaveArea().getWidth());
    const double maxSpp = juce::jmax(minSamplesPerPixel, (double) clip.getNumSamples() / width);
    samplesPerPixel = juce::jlimit(minSamplesPerPixel, juce::jmax(minSamplesPerPixel, maxSpp), samplesPerPixel);

    const double maxStart = juce::jmax(0.0, (double) clip.getNumSamples() - width * samplesPerPixel);
    viewStart = juce::jlimit(0.0, maxStart, viewStart);
}

void WaveformEditorComponent::setView(double newStart, double newSamplesPerPixel)
{
    viewStart = newStart;
    samplesPerPixel = newSamplesPerPixel;
    clampView();
    updateScrollBar();
    repaint();
}

void WaveformEditorComponent::updateScrollBar()
{
    scrollBar.setRangeLimits(0.0, juce::jmax(1.0, (double) clip.getNumSamples()), juce::dontSendNotification);
    scrollBar.setCurrentRange(viewStart, (double) visibleSamples(), juce::dontSendNotification);
}

void WaveformEditorComponent::scrollBarMoved(juce::ScrollBar*, double newRangeStart)
{
    viewStart = newRangeStart;
    clampView();
    repaint();
}

void WaveformEditorComponent::resized()
{
    scrollBar.setBounds(getLocalBounds().removeFromBottom(scrollBarHeight));
    if (followRecording)
        clipChanged();
    else
        setView(viewStart, samplesPerPixel);
}

// ---- Zoom ----

void WaveformEditorComponent::zoomAround(double factor, float anchorX)
{
    followRecording = false;
    const double anchorSample = viewStart + ((double) anchorX - getWaveArea().getX()) * samplesPerPixel;
    const double newSpp = samplesPerPixel * factor;
    setView(anchorSample - ((double) anchorX - getWaveArea().getX()) * newSpp, newSpp);
}

void WaveformEditorComponent::zoomIn()
{
    // Zoom towards the cursor if it's on screen, otherwise the centre.
    const double cursorX = sampleToX((double) cursor);
    const auto area = getWaveArea();
    const float anchor = (cursorX >= area.getX() && cursorX <= area.getRight()) ? (float) cursorX
                                                                               : (float) area.getCentreX();
    zoomAround(0.5, anchor);
}

void WaveformEditorComponent::zoomOut()
{
    zoomAround(2.0, (float) getWaveArea().getCentreX());
}

void WaveformEditorComponent::zoomToFit()
{
    followRecording = false;
    setView(0.0, (double) clip.getNumSamples() / juce::jmax(1, getWaveArea().getWidth()));
}

void WaveformEditorComponent::zoomToSelection()
{
    if (selection.isEmpty())
        return;
    followRecording = false;
    setView((double) selection.getStart(),
            (double) selection.getLength() / juce::jmax(1, getWaveArea().getWidth()));
}

void WaveformEditorComponent::setFollowRecording(bool shouldFollow)
{
    followRecording = shouldFollow;
    if (followRecording)
        clipChanged();
}

// ---- Model updates ----

void WaveformEditorComponent::clipChanged()
{
    const auto length = clip.getNumSamples();
    cursor = juce::jlimit((juce::int64) 0, length, cursor);
    const auto clamped = clip.clampRange(selection);
    if (clamped != selection)
    {
        selection = clamped;
        notifySelection();
    }

    if (followRecording)
    {
        const double minSpan = clip.getSampleRate() > 0.0 ? clip.getSampleRate() * minFollowSeconds : 0.0;
        const double span = juce::jmax((double) length, minSpan);
        viewStart = 0.0;
        samplesPerPixel = juce::jmax(minSamplesPerPixel, span / juce::jmax(1, getWaveArea().getWidth()));
        updateScrollBar();
        repaint();
        return;
    }

    setView(viewStart, samplesPerPixel);
}

void WaveformEditorComponent::setSelection(SampleRange range)
{
    selection = clip.clampRange(range);
    notifySelection();
    repaint();
}

void WaveformEditorComponent::setCursor(juce::int64 sample)
{
    cursor = juce::jlimit((juce::int64) 0, clip.getNumSamples(), sample);

    // Keep the cursor on screen when it is moved from the keyboard.
    const auto visible = visibleSamples();
    if ((double) cursor < viewStart || (double) cursor > viewStart + (double) visible)
        setView((double) cursor - (double) visible * 0.1, samplesPerPixel);
    repaint();
}

void WaveformEditorComponent::selectAll()
{
    setSelection({ 0, clip.getNumSamples() });
}

void WaveformEditorComponent::setPlayhead(double sample)
{
    if (std::abs(sample - playhead) < 0.5)
        return;
    playhead = sample;
    repaint();
}

void WaveformEditorComponent::notifySelection()
{
    if (onSelectionChanged)
        onSelectionChanged();
}

// ---- Mouse ----

void WaveformEditorComponent::mouseDown(const juce::MouseEvent& e)
{
    if (clip.isEmpty())
        return;

    const auto sample = xToSample(e.position.x);

    if (e.mods.isShiftDown())
    {
        // Extend from whichever end of the selection (or the cursor) is farther away.
        const auto anchor = selection.isEmpty() ? cursor
                          : (std::abs(sample - selection.getStart()) > std::abs(sample - selection.getEnd())
                                 ? selection.getStart() : selection.getEnd());
        dragAnchor = anchor;
        setSelection({ juce::jmin(anchor, sample), juce::jmax(anchor, sample) });
        return;
    }

    dragAnchor = sample;
    cursor = sample;
    setSelection({});
}

void WaveformEditorComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (clip.isEmpty())
        return;

    const auto sample = xToSample(e.position.x);
    cursor = juce::jmin(dragAnchor, sample);
    setSelection({ juce::jmin(dragAnchor, sample), juce::jmax(dragAnchor, sample) });
}

void WaveformEditorComponent::mouseDoubleClick(const juce::MouseEvent&)
{
    cursor = 0;
    selectAll();
}

void WaveformEditorComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (clip.isEmpty())
        return;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomAround(wheel.deltaY > 0 ? 0.8 : 1.25, e.position.x);
        return;
    }

    followRecording = false;
    const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    setView(viewStart - (double) delta * (double) visibleSamples() * 0.25, samplesPerPixel);
}

// ---- Painting ----

void WaveformEditorComponent::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    auto rulerArea = getLocalBounds().removeFromTop(rulerHeight);
    const auto waveArea = getWaveArea();

    if (clip.isEmpty())
    {
        g.setColour(rulerColour);
        g.fillRect(rulerArea);
        g.setColour(juce::Colours::grey);
        g.setFont(15.0f);
        g.drawText("Press Record (R) to capture from your microphone", waveArea,
                   juce::Justification::centred);
        return;
    }

    drawRuler(g, rulerArea);

    // Selection behind the waveform.
    if (! selection.isEmpty())
    {
        const float x1 = (float) juce::jmax((double) waveArea.getX(), sampleToX((double) selection.getStart()));
        const float x2 = (float) juce::jmin((double) waveArea.getRight(), sampleToX((double) selection.getEnd()));
        if (x2 > x1)
        {
            g.setColour(selectionColour);
            g.fillRect(juce::Rectangle<float>(x1, (float) waveArea.getY(), juce::jmax(1.0f, x2 - x1),
                                              (float) waveArea.getHeight()));
        }
    }

    const int channels = clip.getNumChannels();
    for (int ch = 0; ch < channels; ++ch)
    {
        auto lane = waveArea.withHeight(waveArea.getHeight() / channels)
                            .withY(waveArea.getY() + ch * waveArea.getHeight() / channels);
        drawChannel(g, ch, lane.reduced(0, 2));

        if (ch > 0)
        {
            g.setColour(rulerColour);
            g.drawHorizontalLine(lane.getY(), (float) lane.getX(), (float) lane.getRight());
        }
    }

    // Edit cursor.
    const double cursorX = sampleToX((double) cursor);
    if (cursorX >= waveArea.getX() && cursorX <= waveArea.getRight())
    {
        g.setColour(cursorColour);
        g.drawVerticalLine((int) cursorX, (float) waveArea.getY(), (float) waveArea.getBottom());
    }

    // Playhead.
    if (playhead >= 0.0)
    {
        const double playX = sampleToX(playhead);
        if (playX >= waveArea.getX() && playX <= waveArea.getRight())
        {
            g.setColour(playheadColour);
            g.fillRect(juce::Rectangle<float>((float) playX - 1.0f, (float) rulerArea.getY(),
                                              2.0f, (float) waveArea.getBottom()));
        }
    }
}

void WaveformEditorComponent::drawRuler(juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour(rulerColour);
    g.fillRect(area);

    const double rate = clip.getSampleRate();
    if (rate <= 0.0)
        return;

    // Smallest "nice" step that leaves room for a label.
    static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
                                    1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600 };
    const double secondsPerPixel = samplesPerPixel / rate;
    double step = steps[std::size(steps) - 1];
    for (double s : steps)
        if (s / secondsPerPixel >= 80.0) { step = s; break; }

    const double firstSecond = viewStart / rate;
    const double lastSecond = (viewStart + (double) visibleSamples()) / rate;

    g.setFont(11.0f);
    for (double t = std::floor(firstSecond / step) * step; t <= lastSecond; t += step)
    {
        const float x = (float) sampleToX(t * rate);
        if (x < area.getX())
            continue;
        g.setColour(juce::Colours::grey);
        g.drawVerticalLine((int) x, (float) area.getBottom() - 6.0f, (float) area.getBottom());
        g.setColour(juce::Colours::lightgrey);
        g.drawText(formatRulerTime(t, step), juce::Rectangle<float>(x + 3.0f, (float) area.getY(), 70.0f,
                                                                     (float) area.getHeight() - 4.0f),
                   juce::Justification::centredLeft);
    }
}

void WaveformEditorComponent::drawChannel(juce::Graphics& g, int channel, juce::Rectangle<int> area) const
{
    const float centreY = (float) area.getCentreY();
    const float halfHeight = (float) area.getHeight() * 0.5f;

    g.setColour(centreLineColour);
    g.drawHorizontalLine((int) centreY, (float) area.getX(), (float) area.getRight());

    const auto length = clip.getNumSamples();
    const float* data = clip.getBuffer().getReadPointer(channel);
    const auto& peaks = clip.getPeaks(channel);
    const bool usePeaks = samplesPerPixel >= MicClip::peakBinSize;

    juce::RectangleList<float> columns;
    for (int px = 0; px < area.getWidth(); ++px)
    {
        const double s0 = viewStart + px * samplesPerPixel;
        if (s0 >= (double) length)
            break;

        auto start = (juce::int64) s0;
        auto end = juce::jmin(length, juce::jmax(start + 1, (juce::int64) std::ceil(s0 + samplesPerPixel)));

        float lo = 0.0f, hi = 0.0f;
        if (usePeaks)
        {
            const auto firstBin = (size_t) (start / MicClip::peakBinSize);
            const auto lastBin = juce::jmin(peaks.size(), (size_t) ((end + MicClip::peakBinSize - 1) / MicClip::peakBinSize));
            lo = 1.0f; hi = -1.0f;
            for (auto bin = firstBin; bin < lastBin; ++bin)
            {
                lo = juce::jmin(lo, peaks[bin].min);
                hi = juce::jmax(hi, peaks[bin].max);
            }
            if (hi < lo)
                lo = hi = 0.0f;
        }
        else
        {
            // Include the next sample too, so neighbouring columns join up when zoomed in.
            end = juce::jmin(length, end + 1);
            auto range = juce::FloatVectorOperations::findMinAndMax(data + start, (int) (end - start));
            lo = range.getStart();
            hi = range.getEnd();
        }

        const float top = centreY - juce::jlimit(-1.0f, 1.0f, hi) * halfHeight;
        const float bottom = centreY - juce::jlimit(-1.0f, 1.0f, lo) * halfHeight;
        columns.addWithoutMerging({ (float) (area.getX() + px), top, 1.0f, juce::jmax(1.0f, bottom - top) });
    }

    g.setColour(waveColour);
    g.fillRectList(columns);
}
