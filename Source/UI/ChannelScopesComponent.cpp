#include "ChannelScopesComponent.h"
#include "SequencerPanelStyle.h"

#include <cmath>

namespace Style = SequencerPanelStyle;

namespace
{
    const juce::Colour tileBg     { 0xff10101f };
    const juce::Colour traceQuiet { 0xff3f9f6f };
    const juce::Colour traceLoud  { 0xff9cff8a };

    // VU levels are linear amplitude; a square root makes quiet channels
    // readable without loud ones flattening against the top.
    float shape(float level) { return std::sqrt(juce::jlimit(0.0f, 1.0f, level)); }
}

ChannelScopesComponent::ChannelScopesComponent()
{
    setInterceptsMouseClicks(false, false);
}

bool ChannelScopesComponent::update(const std::vector<float>& levels)
{
    available = ! levels.empty();
    const bool resizedHistory = history.size() != levels.size();
    if (resizedHistory)
    {
        history.assign(levels.size(), {});
        writePos = 0;
    }

    writePos = (writePos + 1) % historyLength;
    for (size_t c = 0; c < levels.size(); ++c)
        history[c][(size_t) writePos] = shape(levels[c]);

    repaint();
    return resizedHistory;
}

juce::Point<int> ChannelScopesComponent::gridFor(int width) const
{
    const int n = juce::jmax(1, (int) history.size());
    const int cols = juce::jlimit(1, n, (width - 2 * padding + gap) / (minTileW + gap));
    return { cols, (n + cols - 1) / cols };
}

int ChannelScopesComponent::getPreferredHeight(int width) const
{
    if (history.empty()) return Style::sectionHeaderH + 34;
    return Style::sectionHeaderH + 2 * padding + gridFor(width).y * (tileH + gap);
}

void ChannelScopesComponent::paint(juce::Graphics& g)
{
    g.fillAll(Style::panelBg);
    auto area = Style::drawSectionHeader(g, getLocalBounds(),
        "Scopes" + (history.empty() ? juce::String() : "   " + juce::String((int) history.size()) + " channels"));

    if (! available || history.empty())
    {
        g.setColour(Style::dimText);
        g.drawText("Channel levels aren't available for this track", area, juce::Justification::centred);
        return;
    }

    area.reduce(padding, padding);
    const auto grid = gridFor(getWidth());
    const float tileW = ((float) area.getWidth() - (float) gap * (float) (grid.x - 1)) / (float) grid.x;
    const float h = juce::jlimit((float) minTileH, (float) tileH,
                                 ((float) area.getHeight() + gap) / (float) grid.y - (float) gap);

    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));

    for (int c = 0; c < (int) history.size(); ++c)
    {
        const juce::Rectangle<float> tile((float) area.getX() + (float) (c % grid.x) * (tileW + gap),
                                          (float) area.getY() + (float) (c / grid.x) * (h + gap),
                                          tileW, h);
        if (tile.getY() > (float) getHeight()) break;

        g.setColour(tileBg);
        g.fillRoundedRectangle(tile, 2.0f);
        g.setColour(Style::separator);
        g.drawHorizontalLine((int) tile.getCentreY(), tile.getX(), tile.getRight());

        // Mirrored envelope: top edge oldest -> newest, then back along the bottom.
        const auto& hist = history[(size_t) c];
        const float step = tile.getWidth() / (float) (historyLength - 1);
        const float halfH = tile.getHeight() * 0.5f - 1.0f;
        juce::Path env;
        for (int i = 0; i < historyLength; ++i)
        {
            const float v = hist[(size_t) ((writePos + 1 + i) % historyLength)];
            const float x = tile.getX() + (float) i * step;
            if (i == 0) env.startNewSubPath(x, tile.getCentreY() - v * halfH);
            else        env.lineTo(x, tile.getCentreY() - v * halfH);
        }
        for (int i = historyLength - 1; i >= 0; --i)
        {
            const float v = hist[(size_t) ((writePos + 1 + i) % historyLength)];
            env.lineTo(tile.getX() + (float) i * step, tile.getCentreY() + v * halfH);
        }
        env.closeSubPath();

        const float now = hist[(size_t) writePos];
        const auto colour = traceQuiet.interpolatedWith(traceLoud, now);
        g.setColour(colour.withAlpha(0.85f));
        g.fillPath(env);

        if (h >= 22.0f)
        {
            g.setColour(Style::headerText.withAlpha(0.8f));
            g.drawText(juce::String(c + 1), tile.reduced(4.0f, 2.0f), juce::Justification::topLeft, false);
        }
    }
}
