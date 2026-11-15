#include "ChannelActivityComponent.h"
#include "SequencerPanelStyle.h"

#include <cmath>

namespace
{
    using namespace SequencerPanelStyle;

    const juce::Colour idleColumn { 0xff141428 };

    constexpr int kLabelH  = 14;

    constexpr float kNoteDecay = 2.2f;  // per second: a hit fades over ~1.5 s
    constexpr float kCutDecay  = 14.0f; // note cut / key-off: gone almost at once

    // At most this many rows are replayed per poll; a bigger jump is a seek or
    // a position-jump effect rather than rows actually played.
    constexpr int kMaxCatchUpRows = 16;
}

ChannelActivityComponent::ChannelActivityComponent(const ModulePatternData& d) : data(d)
{
    setInterceptsMouseClicks(false, false);
}

void ChannelActivityComponent::ensureChannelCount()
{
    const int n = data.isLoaded() ? data.getNumChannels() : 0;
    if ((int) channels.size() != n)
    {
        channels.assign((size_t) n, Channel{});
        lastPattern = lastRow = lastOrder = -1;
        repaint();
    }
}

void ChannelActivityComponent::processRow(const ModulePattern& pattern, int row)
{
    if (row < 0 || row >= pattern.numRows) return;

    const int n = juce::jmin(pattern.numChannels, (int) channels.size());
    for (int c = 0; c < n; ++c)
    {
        const auto& cell = pattern.at(row, c);
        auto& ch = channels[(size_t) c];

        if (SequencerPanelStyle::parseNote(cell.note) >= 0)
        {
            float level = 1.0f;
            if (cell.vol.startsWithChar('v'))
                level = juce::jlimit(0.15f, 1.0f, (float) cell.vol.substring(1).getIntValue() / 64.0f);

            ch.level     = level;
            ch.decayRate = kNoteDecay;
            ch.colour    = SequencerPanelStyle::instrumentColour(data.parseInstrumentNumber(cell.inst));
            ch.note      = cell.note;
        }
        else if (SequencerPanelStyle::isNoteStop(cell.note))
        {
            ch.decayRate = kCutDecay;
        }
    }
}

void ChannelActivityComponent::update(const PatternPlayPosition& position, bool isPlaying)
{
    ensureChannelCount();

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastTickMs > 0.0 ? (float) juce::jmin(0.25, (nowMs - lastTickMs) / 1000.0) : 0.0f;
    lastTickMs = nowMs;

    bool anyLit = false;
    for (auto& ch : channels)
    {
        if (ch.level > 0.0f)
        {
            ch.level *= std::exp(-ch.decayRate * dt);
            if (ch.level < 0.01f) ch.level = 0.0f;
            anyLit = true;
        }
    }

    const auto* pattern = data.getPattern(position.pattern);
    if (isPlaying && position.isValid() && pattern != nullptr)
    {
        const bool samePattern = position.pattern == lastPattern && position.orderPosition == lastOrder;
        if (! samePattern || position.row != lastRow)
        {
            const int steps = position.row - lastRow;
            if (samePattern && steps > 0 && steps <= kMaxCatchUpRows)
                for (int r = lastRow + 1; r <= position.row; ++r)
                    processRow(*pattern, r);
            else
                processRow(*pattern, position.row);

            lastPattern = position.pattern;
            lastOrder   = position.orderPosition;
            lastRow     = position.row;
            anyLit = true;
        }
    }
    else if (! isPlaying)
    {
        // Resuming should flash the row it resumes on.
        lastPattern = lastRow = lastOrder = -1;
    }

    if (anyLit)
        repaint();
}

void ChannelActivityComponent::paint(juce::Graphics& g)
{
    g.fillAll(panelBg);

    auto area = getLocalBounds();
    auto header = area.removeFromTop(sectionHeaderH);
    g.setColour(headerBg);
    g.fillRect(header);
    g.setColour(separator);
    g.drawHorizontalLine(sectionHeaderH - 1, 0.0f, (float) getWidth());
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
    g.setColour(headerText);
    g.drawText("Channels" + (channels.empty() ? juce::String() : "   " + juce::String((int) channels.size())),
               header.reduced(6, 0), juce::Justification::centredLeft);

    const int n = (int) channels.size();
    if (n == 0)
    {
        g.setColour(dimText);
        g.drawText("No module loaded", area, juce::Justification::centred);
        return;
    }

    area.reduce(6, 6);
    const float gap  = n > 24 ? 1.0f : 3.0f;
    const float colW = ((float) area.getWidth() - gap * (float) (n - 1)) / (float) n;
    const bool labels = colW >= 24.0f;
    const bool numbers = colW >= 12.0f;

    auto barArea = area.toFloat();
    if (numbers) barArea.removeFromTop((float) kLabelH);
    if (labels)  barArea.removeFromBottom((float) kLabelH);

    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), colW >= 34.0f ? 10.5f : 9.0f, juce::Font::plain));

    for (int c = 0; c < n; ++c)
    {
        const auto& ch = channels[(size_t) c];
        const float x = (float) area.getX() + (float) c * (colW + gap);
        const juce::Rectangle<float> column(x, barArea.getY(), colW, barArea.getHeight());

        g.setColour(idleColumn);
        g.fillRoundedRectangle(column, 2.0f);

        if (ch.level > 0.0f)
        {
            // Bar rising from the bottom, plus a glow over the whole column so
            // even thin columns visibly flash.
            const auto bar = column.withTop(column.getBottom() - column.getHeight() * ch.level);
            g.setColour(ch.colour.withAlpha(0.12f + 0.25f * ch.level));
            g.fillRoundedRectangle(column, 2.0f);
            g.setGradientFill(juce::ColourGradient(ch.colour.brighter(0.4f).withAlpha(ch.level), bar.getX(), bar.getY(),
                                                   ch.colour.withAlpha(0.6f + 0.4f * ch.level), bar.getX(), bar.getBottom(),
                                                   false));
            g.fillRoundedRectangle(bar, 2.0f);
        }

        if (numbers)
        {
            g.setColour(headerText);
            g.drawText(juce::String(c + 1), juce::Rectangle<float>(x, (float) area.getY(), colW, (float) kLabelH),
                       juce::Justification::centred, false);
        }
        if (labels && ch.note.isNotEmpty())
        {
            g.setColour(juce::Colours::white.withAlpha(0.35f + 0.65f * ch.level));
            g.drawText(ch.note, juce::Rectangle<float>(x, barArea.getBottom(), colW, (float) kLabelH),
                       juce::Justification::centred, false);
        }
    }
}
