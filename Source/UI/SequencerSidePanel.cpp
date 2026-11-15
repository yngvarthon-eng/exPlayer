#include "SequencerSidePanel.h"
#include "VisualizationComponent.h"
#include "ChannelActivityComponent.h"
#include "PianoRollComponent.h"
#include "TrackInfoComponent.h"
#include "InstrumentNamesComponent.h"
#include "SongMessageComponent.h"
#include "ChannelScopesComponent.h"

#include <cmath>

namespace
{
    // Same palette as the pattern grid (ModulePatternViewer.cpp), so the panel
    // reads as part of the sequencer view rather than a separate window.
    const juce::Colour panelBg      { 0xff0e0e1a };
    const juce::Colour headerBg     { 0xff0c0c18 };
    const juce::Colour headerText   { 0xff8899aa };
    const juce::Colour separator    { 0xff252540 };
    const juce::Colour playColour   { 0xffff7a1a };

    constexpr int kToggleBarH  = 28;
    constexpr int kSectionGap  = 6;
}

// ---- SongOverviewComponent ----
// The song order as a row of blocks, one per order position, wrapping onto as
// many lines as it needs. Played positions are dimmed, the playing one is
// outlined and fills up as its rows go by. Each pattern number gets its own
// hue so repeated patterns -- choruses, mostly -- stand out at a glance.

class SongOverviewComponent : public juce::Component
{
public:
    static constexpr int headerH = 20;
    static constexpr int padding = 6;
    static constexpr int gap     = 3;

    explicit SongOverviewComponent(const ModulePatternData& d) : data(d) {}

    // Called from the panel's timer. Returns true if anything that's drawn
    // changed, so the caller can decide whether the layout needs redoing.
    bool update(const PatternPlayPosition& newPosition)
    {
        bool layoutChanged = rebuildEntriesIfDataChanged();

        const int progressStep = progressStepFor(newPosition);
        if (layoutChanged || newPosition.orderPosition != position.orderPosition
            || newPosition.pattern != position.pattern || progressStep != lastProgressStep)
        {
            position = newPosition;
            lastProgressStep = progressStep;
            repaint();
        }
        return layoutChanged;
    }

    int getPreferredHeight(int width) const
    {
        const auto size = blockSizeFor(width, std::numeric_limits<int>::max());
        return headerH + 2 * padding + linesFor(width, size.x) * (size.y + gap);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(panelBg);

        auto header = getLocalBounds().removeFromTop(headerH);
        g.setColour(headerBg);
        g.fillRect(header);
        g.setColour(separator);
        g.drawHorizontalLine(headerH - 1, 0.0f, (float) getWidth());

        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
        g.setColour(headerText);
        const int current = currentEntry();
        juce::String caption = "Song order";
        if (! entries.empty())
            caption << "   " << (current >= 0 ? juce::String(current + 1) : juce::String("-"))
                    << " / " << (int) entries.size();
        g.drawText(caption, header.reduced(6, 0), juce::Justification::centredLeft);

        if (entries.empty())
        {
            g.setColour(juce::Colour(0xff6a6a8a));
            g.drawText("No song order", getLocalBounds().withTrimmedTop(headerH),
                       juce::Justification::centred);
            return;
        }

        const int innerW = getWidth() - 2 * padding;
        const auto size  = blockSizeFor(innerW + 2 * padding, getHeight() - headerH - 2 * padding);
        const int perLine = juce::jmax(1, (innerW + gap) / (size.x + gap));
        const bool withText = size.x >= 22;
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),
                             size.y >= 20 ? 11.0f : 9.0f, juce::Font::bold));

        for (int i = 0; i < (int) entries.size(); ++i)
        {
            const int x = padding + (i % perLine) * (size.x + gap);
            const int y = headerH + padding + (i / perLine) * (size.y + gap);
            if (y > getHeight()) break;

            const juce::Rectangle<float> r((float) x, (float) y, (float) size.x, (float) size.y);
            const int pat = entries[(size_t) i].pattern;
            const auto hue = juce::Colour::fromHSV(std::fmod(pat * 0.618034f, 1.0f), 0.55f, 0.85f, 1.0f);

            const bool played  = current >= 0 && i < current;
            const bool playing = i == current;

            g.setColour(hue.withAlpha(played ? 0.18f : 0.38f));
            g.fillRoundedRectangle(r, 2.0f);

            if (playing)
            {
                g.setColour(playColour.withAlpha(0.45f));
                g.fillRoundedRectangle(r.withWidth(r.getWidth() * (float) progressFraction()), 2.0f);
                g.setColour(playColour);
                g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.5f);
            }

            if (withText)
            {
                g.setColour(playing ? juce::Colours::white
                                    : juce::Colours::white.withAlpha(played ? 0.35f : 0.8f));
                g.drawText(juce::String(pat + 1), r.toNearestInt(), juce::Justification::centred);
            }
        }
    }

private:
    struct Entry { int orderIndex; int pattern; };

    // Re-reads the order when the module changed under us (the engine reloads
    // its ModulePatternData in place when the playlist moves to another module).
    bool rebuildEntriesIfDataChanged()
    {
        const auto& order = data.getOrder();
        const auto signature = data.isLoaded()
            ? juce::String((int) order.size()) + "|" + juce::String(data.getNumPatterns()) + "|" + data.getTitle()
            : juce::String();
        if (signature == dataSignature)
            return false;

        dataSignature = signature;
        entries.clear();
        if (data.isLoaded())
        {
            for (int o = 0; o < (int) order.size(); ++o)
            {
                const int p = order[(size_t) o];
                if (p == ModulePatternData::kOrderEnd) break;
                if (p >= 0) entries.push_back({ o, p });
            }
        }
        repaint();
        return true;
    }

    int currentEntry() const
    {
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].orderIndex == position.orderPosition)
                return i;
        return -1;
    }

    double progressFraction() const
    {
        const auto* pat = data.getPattern(position.pattern);
        if (pat == nullptr || pat->numRows <= 0 || ! position.isValid()) return 0.0;
        return juce::jlimit(0.0, 1.0, (position.row + position.rowFraction) / pat->numRows);
    }

    // Progress in 1/64 steps, so the fill repaints smoothly without
    // repainting on every timer tick when nothing visible moved.
    int progressStepFor(const PatternPlayPosition& p) const
    {
        const auto* pat = data.getPattern(p.pattern);
        if (pat == nullptr || pat->numRows <= 0 || ! p.isValid()) return -1;
        return (int) (64.0 * (p.row + p.rowFraction) / pat->numRows);
    }

    int linesFor(int width, int blockW) const
    {
        const int innerW = width - 2 * padding;
        const int perLine = juce::jmax(1, (innerW + gap) / (blockW + gap));
        return juce::jmax(1, ((int) entries.size() + perLine - 1) / perLine);
    }

    // Largest block size whose lines all fit in maxHeight; long songs step down
    // to smaller blocks (and eventually to bare swatches without numbers).
    juce::Point<int> blockSizeFor(int width, int maxHeight) const
    {
        static const juce::Point<int> sizes[] = { { 34, 22 }, { 26, 16 }, { 14, 10 }, { 8, 6 } };
        for (const auto& s : sizes)
            if (linesFor(width, s.x) * (s.y + gap) <= maxHeight)
                return s;
        return sizes[std::size(sizes) - 1];
    }

    const ModulePatternData& data;
    juce::String dataSignature { "<none>" };
    std::vector<Entry> entries;
    PatternPlayPosition position;
    int lastProgressStep = -1;
};

// ---- SequencerSidePanel ----

SequencerSidePanel::SequencerSidePanel(AudioEngine& engine, PlaylistManager& playlist)
    : audioEngine(engine)
{
    visualizer   = std::make_unique<VisualizationComponent>(&engine);
    songOverview = std::make_unique<SongOverviewComponent>(engine.getModulePatternData());
    channelActivity = std::make_unique<ChannelActivityComponent>(engine.getModulePatternData());
    pianoRoll    = std::make_unique<PianoRollComponent>(engine.getModulePatternData());
    trackInfo    = std::make_unique<TrackInfoComponent>(engine, playlist);
    instrumentNames = std::make_unique<InstrumentNamesComponent>(engine.getModulePatternData());
    songMessage  = std::make_unique<SongMessageComponent>(engine.getModulePatternData());
    channelScopes = std::make_unique<ChannelScopesComponent>();

    auto addSection = [this](Section s, juce::Component& c, const juce::String& name, bool flexible,
                             std::function<int(int)> preferredHeight)
    {
        SectionEntry e { s, &c, std::make_unique<juce::TextButton>(name), flexible, std::move(preferredHeight) };
        e.toggle->setClickingTogglesState(true);
        e.toggle->setToggleState(true, juce::dontSendNotification);
        e.toggle->setColour(juce::TextButton::buttonColourId,   headerBg);
        e.toggle->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a3a5a));
        e.toggle->setColour(juce::TextButton::textColourOffId,  headerText.withAlpha(0.6f));
        e.toggle->setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
        e.toggle->setWantsKeyboardFocus(false); // keep keys going to the fullscreen window
        e.toggle->onClick = [this]
        {
            resized();
            if (onVisibleSectionsChanged) onVisibleSectionsChanged(getVisibleSections());
        };
        addAndMakeVisible(*e.toggle);
        addAndMakeVisible(c);
        sections.push_back(std::move(e));
    };

    // Top to bottom, as they stack when shown.
    addSection(trackInfoSection, *trackInfo, "Info", false,
               [](int) { return TrackInfoComponent::preferredHeight; });
    addSection(visualizerSection, *visualizer, "Visualizer", true, {});
    addSection(channelActivitySection, *channelActivity, "Channels", false,
               [](int) { return ChannelActivityComponent::preferredHeight; });
    addSection(channelScopesSection, *channelScopes, "Scopes", false,
               [this](int w) { return channelScopes->getPreferredHeight(w); });
    addSection(pianoRollSection, *pianoRoll, "Piano roll", true, {});
    addSection(instrumentNamesSection, *instrumentNames, "Samples", false,
               [this](int w) { return instrumentNames->getPreferredHeight(w); });
    addSection(songMessageSection, *songMessage, "Message", false,
               [this](int w) { return songMessage->getPreferredHeight(w); });
    addSection(songOverviewSection, *songOverview, "Song", false,
               [this](int w) { return songOverview->getPreferredHeight(w); });

    startTimerHz(30);
}

SequencerSidePanel::~SequencerSidePanel()
{
    stopTimer();
}

void SequencerSidePanel::setVisibleSections(int mask)
{
    for (auto& s : sections)
        s.toggle->setToggleState((mask & s.section) != 0, juce::dontSendNotification);
    resized();
}

int SequencerSidePanel::getVisibleSections() const
{
    int mask = 0;
    for (const auto& s : sections)
        if (s.toggle->getToggleState()) mask |= s.section;
    return mask;
}

bool SequencerSidePanel::handleKeyPress(const juce::KeyPress& key)
{
    return visualizer->isVisible() && visualizer->handleDigitHotkey(key);
}

void SequencerSidePanel::timerCallback()
{
    const auto position = audioEngine.getPatternPlayPosition();
    if (channelActivity->isVisible())
        channelActivity->update(position, audioEngine.isPlaying());
    if (pianoRoll->isVisible())
        pianoRoll->update(position);
    if (trackInfo->isVisible())
        trackInfo->update(position);

    // The rest can change height when another module loads.
    bool relayout = songOverview->update(position);
    if (instrumentNames->isVisible())
        relayout |= instrumentNames->update(position, audioEngine.isPlaying());
    if (songMessage->isVisible())
        relayout |= songMessage->tick();
    if (channelScopes->isVisible())
    {
        audioEngine.takeModuleChannelLevels(channelLevels);
        relayout |= channelScopes->update(channelLevels);
    }
    if (relayout)
        resized();
}

void SequencerSidePanel::setTopRightReserve(int width)
{
    if (topRightReserve == width) return;
    topRightReserve = width;
    resized();
}

void SequencerSidePanel::paint(juce::Graphics& g)
{
    g.fillAll(panelBg);
    g.setColour(separator);
    g.drawVerticalLine(0, 0.0f, (float) getHeight());
    g.drawHorizontalLine(toggleBarBottom, 0.0f, (float) getWidth());
}

void SequencerSidePanel::resized()
{
    auto area = getLocalBounds().withTrimmedLeft(1);

    // Toggle bar: one button per section, wrapping onto more lines when the
    // panel is too narrow for them all (the first line leaves room for the
    // window's Exit button).
    {
        constexpr int buttonH = 22, gapX = 4, minButtonW = 74, maxButtonW = 112;
        const int n = (int) sections.size();
        const int firstLineW = area.getWidth() - 8 - topRightReserve;
        const int perLine = juce::jmax(1, juce::jmin(n, (firstLineW + gapX) / (minButtonW + gapX)));
        const int buttonW = juce::jmin(maxButtonW, (firstLineW - gapX * (perLine - 1)) / perLine);
        const int lines = (n + perLine - 1) / perLine;
        const int barH = juce::jmax(kToggleBarH, lines * (buttonH + 3) + 3);

        for (int i = 0; i < n; ++i)
            sections[(size_t) i].toggle->setBounds(area.getX() + 4 + (i % perLine) * (buttonW + gapX),
                                                   area.getY() + 3 + (i / perLine) * (buttonH + 3),
                                                   buttonW, buttonH);
        area.removeFromTop(barH);
        toggleBarBottom = area.getY();
    }
    area.removeFromTop(1);
    area.reduce(kSectionGap, kSectionGap);

    // Fixed-height sections first (capped so one can't crowd out the rest),
    // then the flexible ones split what's left evenly.
    std::vector<SectionEntry*> shown;
    int fixedTotal = 0, flexibleCount = 0;
    for (auto& s : sections)
    {
        s.component->setVisible(s.toggle->getToggleState());
        if (! s.toggle->getToggleState()) continue;
        shown.push_back(&s);
        if (s.flexible) ++flexibleCount;
    }
    if (shown.empty()) return;

    const int gaps = kSectionGap * ((int) shown.size() - 1);
    const int available = juce::jmax(0, area.getHeight() - gaps);
    std::vector<int> heights(shown.size(), 0);
    for (size_t i = 0; i < shown.size(); ++i)
        if (! shown[i]->flexible)
            fixedTotal += heights[i] = juce::jmax(0, shown[i]->preferredHeight(area.getWidth()));

    // With a flexible section shown, the fixed ones together keep to 60%; if
    // they ask for more than their budget, all of them shrink in proportion.
    const int budget = flexibleCount > 0 ? available * 60 / 100 : available;
    if (fixedTotal > budget && fixedTotal > 0)
    {
        const double scale = (double) budget / (double) fixedTotal;
        fixedTotal = 0;
        for (size_t i = 0; i < shown.size(); ++i)
            fixedTotal += heights[i] = (int) (heights[i] * scale);
    }
    const int flexibleH = flexibleCount > 0 ? juce::jmax(0, available - fixedTotal) / flexibleCount : 0;

    for (size_t i = 0; i < shown.size(); ++i)
    {
        const int h = shown[i]->flexible ? flexibleH : heights[i];
        shown[i]->component->setBounds(area.removeFromTop(h));
        area.removeFromTop(kSectionGap);
    }
}
