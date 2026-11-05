#include "InstrumentNamesComponent.h"
#include "SequencerPanelStyle.h"

#include <cmath>

namespace Style = SequencerPanelStyle;

InstrumentNamesComponent::InstrumentNamesComponent(const ModulePatternData& d) : data(d)
{
    setInterceptsMouseClicks(false, false);
}

bool InstrumentNamesComponent::rebuildIfDataChanged()
{
    const auto signature = data.isLoaded()
        ? juce::String((int) data.getInstrumentNames().size()) + "|" + juce::String(data.getNumPatterns()) + "|" + data.getTitle()
        : juce::String();
    if (signature == dataSignature) return false;
    dataSignature = signature;
    glow.clear();
    lastPattern = lastRow = lastOrder = -1;
    repaint();
    return true;
}

void InstrumentNamesComponent::trigger(const ModulePattern& pattern, int row)
{
    if (row < 0 || row >= pattern.numRows) return;
    for (int c = 0; c < pattern.numChannels; ++c)
    {
        const auto& cell = pattern.at(row, c);
        if (Style::parseNote(cell.note) < 0) continue;
        if (const int n = data.parseInstrumentNumber(cell.inst); n > 0)
            glow[n] = 1.0f;
    }
}

bool InstrumentNamesComponent::update(const PatternPlayPosition& position, bool isPlaying)
{
    const bool rebuilt = rebuildIfDataChanged();

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastTickMs > 0.0 ? (float) juce::jmin(0.25, (nowMs - lastTickMs) / 1000.0) : 0.0f;
    lastTickMs = nowMs;

    bool changed = false;
    for (auto it = glow.begin(); it != glow.end();)
    {
        it->second *= std::exp(-2.5f * dt);
        changed = true;
        it = it->second < 0.02f ? glow.erase(it) : std::next(it);
    }

    const auto* pattern = data.getPattern(position.pattern);
    if (isPlaying && position.isValid() && pattern != nullptr)
    {
        const bool samePattern = position.pattern == lastPattern && position.orderPosition == lastOrder;
        if (! samePattern || position.row != lastRow)
        {
            const int steps = position.row - lastRow;
            if (samePattern && steps > 0 && steps <= 16)
                for (int r = lastRow + 1; r <= position.row; ++r) trigger(*pattern, r);
            else
                trigger(*pattern, position.row);
            lastPattern = position.pattern; lastRow = position.row; lastOrder = position.orderPosition;
            changed = true;
        }
    }
    else if (! isPlaying)
    {
        lastPattern = lastRow = lastOrder = -1;
    }

    if (changed) repaint();
    return rebuilt;
}

int InstrumentNamesComponent::columnsFor(int width, int height) const
{
    const int n = (int) data.getInstrumentNames().size();
    const int inner = width - 2 * padding;
    int cols = juce::jmax(1, inner / minColumnW);
    // Short of room: squeeze in more, narrower columns rather than cut names off the end.
    const int rowsFit = juce::jmax(1, height / lineH);
    while (cols < juce::jmax(1, inner / 90) && (n + cols - 1) / cols > rowsFit)
        ++cols;
    return cols;
}

int InstrumentNamesComponent::getPreferredHeight(int width) const
{
    const int n = (int) data.getInstrumentNames().size();
    if (! data.isLoaded() || n == 0) return Style::sectionHeaderH + 34;
    const int cols = columnsFor(width, std::numeric_limits<int>::max() / 2);
    return Style::sectionHeaderH + 2 * padding + ((n + cols - 1) / cols) * lineH;
}

void InstrumentNamesComponent::paint(juce::Graphics& g)
{
    g.fillAll(Style::panelBg);

    const auto& names = data.getInstrumentNames();
    const juce::String kind = data.namesAreSamples() ? "Samples" : "Instruments";
    auto area = Style::drawSectionHeader(g, getLocalBounds(),
        kind + (names.empty() ? juce::String() : "   " + juce::String((int) names.size())));

    if (! data.isLoaded() || names.empty())
    {
        g.setColour(Style::dimText);
        g.drawText(data.isLoaded() ? "No " + kind.toLowerCase() : "No module loaded", area,
                   juce::Justification::centred);
        return;
    }

    area.reduce(padding, padding);
    const int cols = columnsFor(area.getWidth() + 2 * padding, area.getHeight());
    const int rows = ((int) names.size() + cols - 1) / cols;
    const int colW = area.getWidth() / cols;
    const auto font = juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain);
    const int numberW = 26;

    // Column-major, so the list reads down like a tracker's sample list
    // (and multi-line messages hidden in it stay in order).
    for (int i = 0; i < (int) names.size(); ++i)
    {
        const int col = i / rows, row = i % rows;
        const juce::Rectangle<int> cell(area.getX() + col * colW, area.getY() + row * lineH, colW - 6, lineH);
        if (cell.getY() > getHeight()) continue;

        const auto& slot = names[(size_t) i];
        const auto it = glow.find(slot.number);
        const float lit = it != glow.end() ? it->second : 0.0f;
        const auto colour = Style::instrumentColour(slot.number);

        if (lit > 0.0f)
        {
            g.setColour(colour.withAlpha(0.35f * lit));
            g.fillRoundedRectangle(cell.toFloat(), 2.0f);
        }

        g.setFont(font);
        g.setColour(lit > 0.0f ? colour : Style::dimText);
        g.drawText(data.formatInstrumentNumber(slot.number), cell.withWidth(numberW), juce::Justification::centredLeft);

        g.setColour(Style::headerText.brighter(0.3f).interpolatedWith(juce::Colours::white, lit));
        g.drawText(slot.name, cell.withTrimmedLeft(numberW), juce::Justification::centredLeft, true);
    }
}
