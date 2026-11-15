#include "PianoRollComponent.h"
#include "SequencerPanelStyle.h"

namespace
{
    namespace Style = SequencerPanelStyle;

    constexpr int kKeyboardW   = 28;
    constexpr int kMinKeys     = 24;  // never zoom in closer than two octaves
    constexpr int kLookBehind  = 64;  // rows scanned before the view for notes still sounding

    bool isBlackKey(int pitch)
    {
        const int n = ((pitch % 12) + 12) % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    juce::String pitchName(int pitch)
    {
        static const char* names[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
        return juce::String(names[((pitch % 12) + 12) % 12]) + juce::String(pitch / 12);
    }
}

PianoRollComponent::PianoRollComponent(const ModulePatternData& d) : data(d)
{
    setInterceptsMouseClicks(false, false);
}

void PianoRollComponent::rebuildIfDataChanged()
{
    const auto& order = data.getOrder();
    const auto signature = data.isLoaded()
        ? juce::String((int) order.size()) + "|" + juce::String(data.getNumPatterns()) + "|" + data.getTitle()
        : juce::String();
    if (signature == dataSignature) return;
    dataSignature = signature;

    patternCells.clear();
    playableOrder.clear();
    if (! data.isLoaded()) return;

    // Reduce every cell once, and find the pitch range the roll has to cover.
    int lo = 1000, hi = -1000;
    patternCells.resize((size_t) data.getNumPatterns());
    for (int p = 0; p < data.getNumPatterns(); ++p)
    {
        const auto* pat = data.getPattern(p);
        if (pat == nullptr) continue;
        auto& pc = patternCells[(size_t) p];
        pc.numRows = pat->numRows;
        pc.numChannels = pat->numChannels;
        pc.cells.resize(pat->notes.size());
        for (size_t i = 0; i < pat->notes.size(); ++i)
        {
            const auto& note = pat->notes[i];
            auto& cell = pc.cells[i];
            cell.pitch = (short) Style::parseNote(note.note);
            cell.stop  = Style::isNoteStop(note.note);
            cell.instrument = (short) data.parseInstrumentNumber(note.inst);
            if (cell.pitch >= 0)
            {
                lo = juce::jmin(lo, (int) cell.pitch);
                hi = juce::jmax(hi, (int) cell.pitch);
            }
        }
    }

    if (hi < lo) { lo = 48; hi = 72; }
    lo -= 1; hi += 1;                         // a key of air on each side
    const int missing = kMinKeys - (hi - lo + 1);
    if (missing > 0) { lo -= missing / 2; hi += missing - missing / 2; }
    lowestPitch = lo;
    highestPitch = hi;

    for (int o = 0; o < (int) order.size(); ++o)
    {
        const int p = order[(size_t) o];
        if (p == ModulePatternData::kOrderEnd) break;
        if (p >= 0 && p < (int) patternCells.size()) playableOrder.push_back(o);
    }
}

const PianoRollComponent::PatternCells* PianoRollComponent::cellsFor(int pattern)
{
    if (pattern < 0 || pattern >= (int) patternCells.size()) return nullptr;
    const auto& pc = patternCells[(size_t) pattern];
    return pc.numRows > 0 ? &pc : nullptr;
}

std::vector<PianoRollComponent::TimelineRow> PianoRollComponent::buildTimeline(int before, int after,
                                                                               int& currentIndex) const
{
    std::vector<TimelineRow> rows;
    currentIndex = -1;
    if (! position.isValid()) return rows;

    const auto& order = data.getOrder();
    auto patternAt = [&](int k) { return order[(size_t) playableOrder[(size_t) k]]; };
    auto rowsIn    = [&](int pattern) { return pattern < (int) patternCells.size() ? patternCells[(size_t) pattern].numRows : 0; };

    // Where we are in the playable order; unknown order position (or the
    // user-browsed pattern isn't the playing one) -> just this pattern.
    int k = -1;
    for (int i = 0; i < (int) playableOrder.size(); ++i)
        if (playableOrder[(size_t) i] == position.orderPosition) { k = i; break; }

    std::vector<TimelineRow> back;
    {
        int kk = k, pat = position.pattern, r = position.row;
        for (int n = 0; n < before; ++n)
        {
            if (--r < 0)
            {
                if (kk <= 0) break;
                pat = patternAt(--kk);
                r = rowsIn(pat) - 1;
                if (r < 0) break;
            }
            back.push_back({ pat, r });
        }
    }
    rows.assign(back.rbegin(), back.rend());
    currentIndex = (int) rows.size();
    rows.push_back({ position.pattern, position.row });
    {
        int kk = k, pat = position.pattern, r = position.row;
        for (int n = 0; n < after; ++n)
        {
            if (++r >= rowsIn(pat))
            {
                if (kk < 0 || kk + 1 >= (int) playableOrder.size()) break;
                pat = patternAt(++kk);
                r = 0;
                if (rowsIn(pat) <= 0) break;
            }
            rows.push_back({ pat, r });
        }
    }
    return rows;
}

void PianoRollComponent::update(const PatternPlayPosition& newPosition)
{
    rebuildIfDataChanged();
    const bool moved = newPosition.pattern != position.pattern || newPosition.row != position.row
                    || std::abs(newPosition.rowFraction - position.rowFraction) > 0.02
                    || newPosition.orderPosition != position.orderPosition;
    position = newPosition;
    if (moved) repaint();
}

void PianoRollComponent::paint(juce::Graphics& g)
{
    g.fillAll(Style::panelBg);

    const bool hasData = ! patternCells.empty();
    auto area = Style::drawSectionHeader(g, getLocalBounds(),
        hasData ? "Piano roll   " + pitchName(lowestPitch) + " .. " + pitchName(highestPitch)
                : juce::String("Piano roll"));

    if (! hasData)
    {
        g.setColour(Style::dimText);
        g.drawText("No module loaded", area, juce::Justification::centred);
        return;
    }

    area.reduce(0, 2);
    const auto keyboard = area.removeFromLeft(kKeyboardW).toFloat();
    const auto roll = area.toFloat();

    const int numKeys = highestPitch - lowestPitch + 1;
    const float keyH = roll.getHeight() / (float) numKeys;
    auto yFor = [&](int pitch) { return roll.getBottom() - (float) (pitch - lowestPitch + 1) * keyH; };

    const float rowW = roll.getWidth() / (float) (rowsBehind + rowsAhead);
    const float playX = roll.getX() + (float) rowsBehind * rowW;

    // Lanes: black-key rows a shade darker, a line under every C.
    for (int p = lowestPitch; p <= highestPitch; ++p)
    {
        const float y = yFor(p);
        if (isBlackKey(p))
        {
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillRect(roll.getX(), y, roll.getWidth(), keyH);
        }
        if (((p % 12) + 12) % 12 == 0)
        {
            g.setColour(Style::separator);
            g.drawHorizontalLine((int) (y + keyH), roll.getX(), roll.getRight());
        }
    }

    int currentIndex = -1;
    const auto timeline = buildTimeline(rowsBehind + kLookBehind, rowsAhead + 1, currentIndex);
    if (currentIndex < 0) return;

    const double fraction = juce::jlimit(0.0, 1.0, position.rowFraction);
    auto xFor = [&](double timelineIndex)
    {
        return playX + (float) ((timelineIndex - currentIndex - fraction) * rowW);
    };

    // Beat grid: every 4th row faint, every 16th stronger, pattern starts in
    // the play colour.
    for (int i = 0; i < (int) timeline.size(); ++i)
    {
        const float x = xFor(i);
        if (x < roll.getX() || x > roll.getRight()) continue;
        const int r = timeline[(size_t) i].row;
        if (r == 0)               g.setColour(Style::playColour.withAlpha(0.35f));
        else if (r % 16 == 0)     g.setColour(Style::separator.brighter(0.4f));
        else if (r % 4 == 0)      g.setColour(Style::separator);
        else continue;
        g.drawVerticalLine((int) x, roll.getY(), roll.getBottom());
    }

    // Notes, channel by channel: each lasts until the channel's next note or stop.
    std::vector<juce::Colour> soundingKey((size_t) numKeys, juce::Colours::transparentBlack);
    const double playPos = currentIndex + fraction;

    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(roll.toNearestInt());

        int maxChannels = 0;
        for (const auto& row : timeline)
            if (const auto* pc = cellsFor(row.pattern)) maxChannels = juce::jmax(maxChannels, pc->numChannels);

        for (int c = 0; c < maxChannels; ++c)
        {
            int startIndex = -1, pitch = -1, instrument = 0;
            auto emit = [&](int endIndex)
            {
                if (startIndex < 0 || pitch < lowestPitch || pitch > highestPitch) return;
                const float x0 = xFor(startIndex), x1 = xFor(endIndex);
                if (x1 < roll.getX() || x0 > roll.getRight()) return;

                const bool sounding = playPos >= startIndex && playPos < endIndex;
                const auto colour = Style::instrumentColour(instrument);
                const juce::Rectangle<float> r(x0 + 0.5f, yFor(pitch) + 0.5f,
                                               juce::jmax(2.0f, x1 - x0 - 1.0f), juce::jmax(2.0f, keyH - 1.0f));
                g.setColour(colour.withAlpha(sounding ? 1.0f : (endIndex <= playPos ? 0.35f : 0.7f)));
                g.fillRoundedRectangle(r, juce::jmin(2.0f, keyH * 0.3f));
                if (sounding)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.7f));
                    g.drawRoundedRectangle(r, juce::jmin(2.0f, keyH * 0.3f), 1.0f);
                    soundingKey[(size_t) (pitch - lowestPitch)] = colour;
                }
            };

            for (int i = 0; i < (int) timeline.size(); ++i)
            {
                const auto* pc = cellsFor(timeline[(size_t) i].pattern);
                if (pc == nullptr || c >= pc->numChannels) continue;
                const auto& cell = pc->cells[(size_t) (timeline[(size_t) i].row * pc->numChannels + c)];
                if (cell.pitch >= 0 || cell.stop)
                {
                    emit(i);
                    startIndex = cell.pitch >= 0 ? i : -1;
                    pitch      = cell.pitch;
                    instrument = cell.instrument;
                }
            }
            emit((int) timeline.size());
        }

        // Play line.
        g.setColour(Style::playColour);
        g.fillRect(playX - 1.0f, roll.getY(), 2.0f, roll.getHeight());
    }

    // Keyboard: white keys, then black keys a little narrower, lit while sounding.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int p = lowestPitch; p <= highestPitch; ++p)
        {
            const bool black = isBlackKey(p);
            if (black != (pass == 1)) continue;
            auto key = juce::Rectangle<float>(keyboard.getX(), yFor(p), keyboard.getWidth() - (black ? 8.0f : 1.0f), keyH);
            const auto lit = soundingKey[(size_t) (p - lowestPitch)];
            g.setColour(! lit.isTransparent() ? lit : (black ? juce::Colour(0xff1a1a24) : juce::Colour(0xffc8ccd4)));
            g.fillRect(key.reduced(0.0f, black ? 0.0f : 0.5f));
            if (! black && ((p % 12) + 12) % 12 == 0 && keyH >= 7.0f)
            {
                g.setColour(juce::Colours::black.withAlpha(0.6f));
                g.setFont(juce::Font(juce::jmin(9.0f, keyH + 1.0f)));
                g.drawText(pitchName(p), key.withTrimmedRight(2.0f), juce::Justification::centredRight, false);
            }
        }
    }
}
