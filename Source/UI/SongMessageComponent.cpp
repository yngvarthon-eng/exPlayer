#include "SongMessageComponent.h"
#include "SequencerPanelStyle.h"

namespace Style = SequencerPanelStyle;

SongMessageComponent::SongMessageComponent(const ModulePatternData& d) : data(d)
{
    setInterceptsMouseClicks(false, false);
}

bool SongMessageComponent::rebuildIfDataChanged()
{
    const auto message = data.isLoaded() ? data.getMessage() : juce::String();
    if (message == shownMessage) return false;
    shownMessage = message;

    lines = juce::StringArray::fromLines(message.replace("\t", "    "));
    while (! lines.isEmpty() && lines[lines.size() - 1].trim().isEmpty())
        lines.remove(lines.size() - 1);

    longestLine = 0;
    for (const auto& l : lines) longestLine = juce::jmax(longestLine, l.length());

    scroll = 0.0;
    pauseUntilMs = juce::Time::getMillisecondCounterHiRes() + pauseMs;
    repaint();
    return true;
}

float SongMessageComponent::fontSizeFor(int width) const
{
    // Monospaced glyphs are ~0.6 em wide.
    const float fit = longestLine > 0 ? (float) (width - 2 * padding) / ((float) longestLine * 0.6f) : 12.0f;
    return juce::jlimit(8.0f, 13.0f, fit);
}

int SongMessageComponent::getPreferredHeight(int width) const
{
    if (lines.isEmpty()) return Style::sectionHeaderH + 34;
    const int lineH = (int) std::ceil(fontSizeFor(width) * 1.25f);
    return Style::sectionHeaderH + 2 * padding + juce::jmin(lines.size(), maxPreferredLines) * lineH;
}

bool SongMessageComponent::tick()
{
    const bool rebuilt = rebuildIfDataChanged();

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastTickMs > 0.0 ? juce::jmin(0.25, (nowMs - lastTickMs) / 1000.0) : 0.0;
    lastTickMs = nowMs;

    const int lineH = (int) std::ceil(fontSizeFor(getWidth()) * 1.25f);
    const int viewH = getHeight() - Style::sectionHeaderH - 2 * padding;
    const int overflow = lines.size() * lineH - viewH;
    if (overflow <= 0 || viewH <= 0)
    {
        if (scroll != 0.0) { scroll = 0.0; repaint(); }
        return rebuilt;
    }

    if (nowMs < pauseUntilMs) return rebuilt;

    if (scroll >= overflow)
    {
        // Held at the bottom for a moment; now back to the top.
        scroll = 0.0;
        pauseUntilMs = nowMs + pauseMs;
    }
    else
    {
        scroll = juce::jmin((double) overflow, scroll + scrollPixelsPerSecond * dt);
        if (scroll >= overflow) pauseUntilMs = nowMs + pauseMs;
    }
    repaint();
    return rebuilt;
}

void SongMessageComponent::paint(juce::Graphics& g)
{
    g.fillAll(Style::panelBg);
    auto area = Style::drawSectionHeader(g, getLocalBounds(), "Song message");

    if (lines.isEmpty())
    {
        g.setColour(Style::dimText);
        g.drawText(data.isLoaded() ? "This song has no message" : "No module loaded", area,
                   juce::Justification::centred);
        return;
    }

    area.reduce(padding, padding);
    const float size = fontSizeFor(getWidth());
    const int lineH = (int) std::ceil(size * 1.25f);

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
    g.setColour(juce::Colour(0xffc8d4e0));

    const int top = area.getY() - (int) scroll;
    for (int i = 0; i < lines.size(); ++i)
    {
        const int y = top + i * lineH;
        if (y + lineH < area.getY() || y > area.getBottom()) continue;
        g.drawText(lines[i], area.getX(), y, area.getWidth(), lineH, juce::Justification::centredLeft, false);
    }
}
