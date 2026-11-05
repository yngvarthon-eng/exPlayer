#include "TrackInfoComponent.h"
#include "SequencerPanelStyle.h"

#include <cmath>

namespace
{
    namespace Style = SequencerPanelStyle;

    juce::String formatTime(double seconds)
    {
        if (seconds < 0.0 || ! std::isfinite(seconds)) seconds = 0.0;
        const int total = (int) seconds;
        const int h = total / 3600, m = (total / 60) % 60, s = total % 60;
        return h > 0 ? juce::String::formatted("%d:%02d:%02d", h, m, s)
                     : juce::String::formatted("%d:%02d", m, s);
    }

    // Descriptive names for the common module extensions; anything else is
    // shown as its extension.
    juce::String formatName(const juce::File& file)
    {
        const auto ext = file.getFileExtension().toLowerCase().trimCharactersAtStart(".");
        static const std::pair<const char*, const char*> known[] = {
            { "mod", "ProTracker module (MOD)" },  { "xm", "FastTracker 2 module (XM)" },
            { "it", "Impulse Tracker module (IT)" }, { "s3m", "Scream Tracker 3 module (S3M)" },
            { "mptm", "OpenMPT module (MPTM)" },     { "xtp", "exTracker song (XTP)" },
            { "xtd", "exTracker song (XTD)" },
        };
        for (const auto& [e, name] : known)
            if (ext == e) return name;
        return ext.toUpperCase() + " module";
    }
}

TrackInfoComponent::TrackInfoComponent(AudioEngine& engine, PlaylistManager& playlist)
    : audioEngine(engine), playlistManager(playlist)
{
    setInterceptsMouseClicks(false, false);
}

void TrackInfoComponent::update(const PatternPlayPosition& newPosition)
{
    const int second = (int) audioEngine.getCurrentPlaybackTime();
    if (second != lastSecond || newPosition.orderPosition != position.orderPosition
        || newPosition.pattern != position.pattern || newPosition.row != position.row)
    {
        lastSecond = second;
        position = newPosition;
        repaint();
    }
}

void TrackInfoComponent::paint(juce::Graphics& g)
{
    g.fillAll(Style::panelBg);
    auto area = Style::drawSectionHeader(g, getLocalBounds(), "Track info").reduced(10, 6);
    // Time and progress keep the bottom line even when the panel squeezes this
    // section; the text above gets what's left.
    auto bottom = area.removeFromBottom(18);
    const double now = audioEngine.getCurrentPlaybackTime();
    const double total = audioEngine.getTotalDuration();
    g.setColour(Style::headerText);
    g.drawText(formatTime(now) + " / " + (total > 0.0 ? formatTime(total) : juce::String("--:--")),
               bottom.removeFromRight(110), juce::Justification::centredRight);
    auto bar = bottom.reduced(0, 7).toFloat().withTrimmedRight(10.0f);
    g.setColour(Style::separator);
    g.fillRoundedRectangle(bar, 2.0f);
    if (total > 0.0)
    {
        g.setColour(Style::playColour);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * (float) juce::jlimit(0.0, 1.0, now / total)), 2.0f);
    }

    juce::Graphics::ScopedSaveState textClip(g);
    g.reduceClipRegion(area.withTop(0));

    const auto& data = audioEngine.getModulePatternData();
    const auto* item = playlistManager.getCurrentItem();
    if (item == nullptr && ! data.isLoaded())
    {
        g.setColour(Style::dimText);
        g.drawText("Nothing playing", area, juce::Justification::centred);
        return;
    }

    // Title, then artist / file underneath.
    juce::String title = data.isLoaded() ? data.getTitle().trim() : juce::String();
    if (title.isEmpty() && item != nullptr) title = item->name;
    g.setColour(juce::Colour(0xffdde6f0));
    g.setFont(juce::Font(17.0f, juce::Font::bold));
    g.drawFittedText(title.isNotEmpty() ? title : "Untitled", area.removeFromTop(22),
                     juce::Justification::centredLeft, 1, 0.75f);

    juce::String subtitle;
    if (item != nullptr)
    {
        if (item->artist.isNotEmpty()) subtitle << item->artist << "  -  ";
        subtitle << item->file.getFileName();
    }
    g.setColour(Style::headerText);
    g.setFont(juce::Font(12.0f));
    g.drawFittedText(subtitle, area.removeFromTop(16), juce::Justification::centredLeft, 1, 0.75f);
    area.removeFromTop(6);

    // Label/value facts: the long ones on lines of their own, the short ones
    // two to a line.
    using Fact = std::pair<juce::String, juce::String>;
    std::vector<Fact> shortFacts;
    Fact format, where;

    if (item != nullptr) format = { "Format", formatName(item->file) };
    if (data.isLoaded())
    {
        int songLength = 0;
        for (int p : data.getOrder())
        {
            if (p == ModulePatternData::kOrderEnd) break;
            if (p >= 0) ++songLength;
        }
        shortFacts.push_back({ "Channels", juce::String(data.getNumChannels()) });
        shortFacts.push_back({ "Patterns", juce::String(data.getNumPatterns()) });
        shortFacts.push_back({ "Song", juce::String(songLength) + " positions" });

        if (position.isValid())
        {
            const auto* pat = data.getPattern(position.pattern);
            where = { "Position", "order " + juce::String(position.orderPosition + 1)
                                  + ", pattern " + juce::String(position.pattern + 1)
                                  + ", row " + juce::String(position.row)
                                  + (pat != nullptr ? " / " + juce::String(pat->numRows) : juce::String()) };
        }
    }

    constexpr int lineH = 15, labelW = 74;
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    auto drawFact = [&](const Fact& f, juce::Rectangle<int> cell)
    {
        g.setColour(Style::dimText);
        g.drawText(f.first, cell.removeFromLeft(labelW), juce::Justification::centredLeft);
        g.setColour(Style::headerText.brighter(0.3f));
        g.drawText(f.second, cell, juce::Justification::centredLeft, true);
    };

    if (format.first.isNotEmpty()) drawFact(format, area.removeFromTop(lineH));
    for (size_t i = 0; i < shortFacts.size(); i += 2)
    {
        auto line = area.removeFromTop(lineH);
        auto left = line.removeFromLeft(juce::jmin(line.getWidth() / 2, 200));
        drawFact(shortFacts[i], left);
        if (i + 1 < shortFacts.size()) drawFact(shortFacts[i + 1], line);
    }
    if (where.first.isNotEmpty()) drawFact(where, area.removeFromTop(lineH));
}
