#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include "../Xtp/XtpSong.h"

/**
 * Holds the display data for a single note cell in a tracker pattern.
 * Strings are in libopenmpt's notation, normalised so the viewer's empty
 * sentinels are consistent: "---" (note), "--" (inst), ".." (vol), "..." (effect).
 */
struct ModuleNote
{
    juce::String note;    // "C-5", "---" (empty), "^^^" (cut), "===" (key-off)
    juce::String inst;    // "01", "--" (empty)
    juce::String vol;     // "v40", ".." (empty)
    juce::String effect;  // "A01", "..." (empty)

    bool isEmpty() const { return note == "---" && inst == "--"; }

    juce::String getNoteString()   const { return note; }
    juce::String getInstString()   const { return inst; }
    juce::String getVolString()    const { return vol; }
    juce::String getEffectString() const { return effect; }
};

struct ModulePattern
{
    int numRows     = 0;
    int numChannels = 0;
    std::vector<ModuleNote> notes;  // [row * numChannels + channel]

    const ModuleNote& at(int row, int channel) const
    {
        return notes[(size_t)(row * numChannels + channel)];
    }
};

/**
 * Where playback is in a module, as the pattern viewer needs it to scroll
 * smoothly: whole row plus how far into it, and the song-order position so the
 * viewer can show the neighbouring patterns.
 */
struct PatternPlayPosition
{
    int    pattern       = -1;
    int    row           = -1;
    double rowFraction   = 0.0;  // 0..1 through `row`
    int    orderPosition = -1;   // index into ModulePatternData::getOrder(), -1 unknown
    double rowSeconds    = 0.0;  // current row length (glide speed), 0 unknown

    bool isValid() const { return pattern >= 0 && row >= 0; }
};

class ModulePatternData
{
public:
    ModulePatternData() = default;

    bool loadFromFile(const juce::File& file);

    // Populates from an already-parsed .xtp song (Source/Xtp/XtpSong.h), so the
    // existing pattern-viewer UI (ModulePatternViewer/PatternGridComponent) can
    // display .xtp patterns without any changes -- ModuleNote's fields are
    // already plain display strings, not libopenmpt-specific.
    bool loadFromXtpSong(const xtp::Song& song);

    void clear();

    bool isLoaded()       const { return loaded; }
    int  getNumChannels() const { return numChannels; }
    int  getNumPatterns() const { return (int)patterns.size(); }

    const ModulePattern* getPattern(int index) const
    {
        if (index >= 0 && index < (int)patterns.size())
            return &patterns[(size_t)index];
        return nullptr;
    }

    juce::String getTitle() const { return title; }

    // Song order: pattern index per position, or kOrderSkip for a skipped
    // entry ("+++", or an .xtp entry that names no pattern) and kOrderEnd for
    // an end-of-song marker ("---").
    static constexpr int kOrderSkip = -1;
    static constexpr int kOrderEnd  = -2;
    const std::vector<int>& getOrder() const { return order; }

    // Where each order position starts, in seconds from the start of the song
    // (parallel to getOrder()); -1 for positions playback never reaches, and
    // empty when the format can't tell (.xtp). Used to skip by pattern.
    const std::vector<double>& getOrderStartTimes() const { return orderStartTimes; }

    // The module's instrument list -- or its sample list, for formats without
    // instruments -- as the numbers pattern cells use. Tracker musicians have
    // long hidden greetings and credits in these names.
    struct NamedSlot { int number; juce::String name; };
    const std::vector<NamedSlot>& getInstrumentNames() const { return instrumentNames; }
    bool namesAreSamples() const { return instrumentNamesAreSamples; }

    // The pattern cell's instrument column ("01", "--") as the number used in
    // getInstrumentNames(); 0 for none. (libopenmpt prints these in hex,
    // .xtp songs in decimal.)
    int parseInstrumentNumber(const juce::String& inst) const
    {
        if (inst.isEmpty() || inst.containsOnly("-")) return 0;
        return instrumentsInDecimal ? inst.getIntValue() : inst.getHexValue32();
    }

    // The other way round: an instrument number as pattern cells print it.
    juce::String formatInstrumentNumber(int number) const
    {
        return instrumentsInDecimal ? juce::String(number).paddedLeft('0', 2)
                                    : juce::String::toHexString(number).toUpperCase().paddedLeft('0', 2);
    }

    // The song message (often ASCII art, credits or greetings) and the
    // artist, when the file has them.
    juce::String getMessage() const { return message; }
    juce::String getArtist()  const { return artist; }

private:
    bool         loaded      = false;
    int          numChannels = 0;
    juce::String title;
    std::vector<ModulePattern> patterns;
    std::vector<int> order;
    std::vector<double> orderStartTimes;
    std::vector<NamedSlot> instrumentNames;
    bool instrumentNamesAreSamples = false;
    bool instrumentsInDecimal = false;
    juce::String message, artist;
};
