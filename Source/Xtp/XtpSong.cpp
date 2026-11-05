#include "XtpSong.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace xtp
{

namespace
{
    // Reads whitespace-separated integer tokens until one fails to parse as an
    // integer (or EOF), then rewinds the stream to just before that token. Used
    // for trailer arrays whose length isn't implied by the header (MIDI_MAP,
    // CHANNEL_INSTRUMENTS, ...) -- none of which affect audio playback, so we
    // only need to consume them without desyncing the parse, not interpret them.
    std::vector<long long> readNumericTokenSequence(std::istream& in)
    {
        std::vector<long long> result;
        for (;;)
        {
            auto pos = in.tellg();
            std::string tok;
            if (!(in >> tok))
                break;

            char* end = nullptr;
            long long value = std::strtoll(tok.c_str(), &end, 10);
            if (end == tok.c_str() || *end != '\0')
            {
                in.clear();
                in.seekg(pos);
                break;
            }
            result.push_back(value);
        }
        return result;
    }

    // Inverse of exTracker's escapeModuleMessage (main.cpp): \\ \n \r \t escapes.
    juce::String unescapeModuleMessage(const std::string& escaped)
    {
        std::string result;
        result.reserve(escaped.size());
        for (std::size_t i = 0; i < escaped.size(); ++i)
        {
            char c = escaped[i];
            if (c == '\\' && i + 1 < escaped.size())
            {
                char next = escaped[++i];
                switch (next)
                {
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case '\\': result += '\\'; break;
                    default:   result += next; break;
                }
            }
            else
            {
                result += c;
            }
        }
        return juce::String::fromUTF8(result.data(), (int) result.size());
    }

    bool parseCellLine(std::istream& in, int parsedRow, bool withSample, Pattern& pattern)
    {
        int channel = 0, hasNote = 0, note = -1, instrument = 0, sample = 0xFFFF;
        int gateTicks = 0, velocity = 100, retrigger = 0, effectCommand = 0, effectValue = 0;

        if (withSample)
            in >> channel >> hasNote >> note >> instrument >> sample >> gateTicks
               >> velocity >> retrigger >> effectCommand >> effectValue;
        else
            in >> channel >> hasNote >> note >> instrument >> gateTicks
               >> velocity >> retrigger >> effectCommand >> effectValue;

        if (!in)
            return false;

        // Out-of-range cells are silently dropped (matches exTracker's own loader) --
        // not a parse failure, since the rest of the line was well-formed.
        if (parsedRow < 0 || channel < 0 || parsedRow >= pattern.rows || channel >= pattern.channels)
            return true;

        Step step;
        step.hasNote = hasNote != 0;
        step.note = note;
        step.instrument = (std::uint8_t) juce::jlimit(0, 255, instrument);
        step.gateTicks = (std::uint32_t) juce::jmax(0, gateTicks);
        step.velocity = (std::uint8_t) juce::jlimit(1, 127, velocity);
        step.retrigger = retrigger != 0;
        step.effectCommand = (std::uint8_t) juce::jlimit(0, 255, effectCommand);
        step.effectValue = (std::uint8_t) juce::jlimit(0, 255, effectValue);
        if (withSample && sample != 0xFFFF)
            step.sample = (std::uint16_t) juce::jlimit(0, 65535, sample);

        pattern.at(parsedRow, channel) = step;
        return true;
    }

    bool isKnownTailToken(const std::string& t)
    {
        // Mirrors isSongTailToken in exTracker's loadPatternFromFile (main.cpp).
        static const char* const known[] = {
            "SONG_ORDER", "PATTERN_SWING", "INSERT_SWING_INHERIT", "ROW_EDIT_SCOPE",
            "TRANSPORT", "MIDI_MAP", "MIDI_TRANSPORT", "MIDI_EDITOR_CC_MAP",
            "SAMPLE_BANK", "SAMPLE_ENTRY", "INSTRUMENT_ASSIGN", "INSTR_SAMPLE_SLOT",
            "INSTRUMENT_NAME", "INSTRUMENT_PARAM", "INSTRUMENT_PRESET", "INSTRUMENT_FILTER",
            "INSTRUMENT_EFFECTS", "INSTRUMENT_PITCH", "INSTRUMENT_REVERB", "INSTRUMENT_DEPTH",
            "MODULE_MESSAGE", "CHANNEL_INSTRUMENTS", "CHANNEL_MUTED", "CHANNEL_VOLUME",
            "CHANNEL_NAMES", "CHANNEL_FILTERS", "CHANNEL_TRACKS", "REVERB"
        };
        for (auto* k : known)
            if (t == k)
                return true;
        return t.rfind("RECORD_", 0) == 0;
    }
}

void Song::addSampleEntry(int slot, const juce::String& name, const std::string& rawPath,
                          const juce::File& moduleDirectory)
{
    if (slot < 0 || slot > 256)
        return;

    const juce::String pathStr = juce::String::fromUTF8(rawPath.data(), (int) rawPath.size());
    juce::File path = juce::File::isAbsolutePath(pathStr) ? juce::File(pathStr)
                                                           : moduleDirectory.getChildFile(pathStr);

    samples[slot] = SampleEntry { name, path };
}

bool Song::applyTrailerToken(std::istream& in, const std::string& token, const juce::File& moduleDirectory)
{
    if (token == "SONG_ORDER")
    {
        auto values = readNumericTokenSequence(in);
        songOrder.assign(values.begin(), values.end());
    }
    else if (token == "PATTERN_SWING")
    {
        for (std::size_t i = 0; i < patternSwing.size(); ++i)
        {
            int sw = 50;
            if (!(in >> sw))
                return false;
            patternSwing[i] = (std::uint8_t) juce::jlimit(50, 75, sw);
        }
    }
    else if (token == "INSERT_SWING_INHERIT" || token == "ROW_EDIT_SCOPE")
    {
        int v = 0;
        in >> v; // editor-only UX flags, no effect on playback
    }
    else if (token == "TRANSPORT")
    {
        std::string line;
        std::getline(in, line);
        std::istringstream ts(line);
        double bpm = tempoBpm;
        int tpb = ticksPerBeat;
        int tpr = ticksPerRow;
        if (ts >> bpm >> tpb)
        {
            if (bpm > 0.0) tempoBpm = bpm;
            if (tpb > 0)   ticksPerBeat = tpb;
            if (ts >> tpr && tpr > 0)
                ticksPerRow = tpr;
        }
    }
    else if (token == "MIDI_TRANSPORT")
    {
        long long timeoutMs = 0;
        int lockTempo = 0;
        in >> timeoutMs >> lockTempo; // MIDI clock sync settings, not audio-relevant
    }
    else if (token == "MIDI_MAP" || token == "MIDI_EDITOR_CC_MAP"
             || token == "CHANNEL_INSTRUMENTS")
    {
        readNumericTokenSequence(in); // consumed, not interpreted: none affect playback
    }
    else if (token.rfind("RECORD_", 0) == 0
             || token == "INSTRUMENT_NAME" || token == "INSTRUMENT_PRESET"
             || token == "INSTRUMENT_FILTER" || token == "INSTRUMENT_EFFECTS"
             || token == "INSTRUMENT_REVERB" || token == "INSTRUMENT_DEPTH"
             || token == "CHANNEL_NAMES" || token == "CHANNEL_FILTERS" || token == "REVERB")
    {
        // One line each, possibly with quoted strings: skip the whole line so a
        // name never desyncs the parse. Not rendered (plugin presets, per-
        // instrument/channel filters and effects, and reverb are exTracker DSP
        // soundplayer2 doesn't reproduce).
        std::string line;
        std::getline(in, line);
    }
    else if (token == "CHANNEL_MUTED")
    {
        auto values = readNumericTokenSequence(in);
        channelMuted.assign(values.size(), false);
        for (std::size_t i = 0; i < values.size(); ++i)
            channelMuted[i] = values[i] != 0;
    }
    else if (token == "CHANNEL_TRACKS")
    {
        // <first> <last> <muted> <volume%> "<name>", repeated, on one line.
        std::string line;
        std::getline(in, line);
        std::istringstream values(line);
        int first = 0, last = 0, muted = 0, pct = 100;
        std::string name;
        while (values >> first >> last >> muted >> pct >> std::quoted(name))
            if (first >= 0 && first <= last)
                tracks.push_back({ first, last, muted != 0, (float) juce::jlimit(0, 200, pct) / 100.0f });
    }
    else if (token == "INSTR_SAMPLE_SLOT")
    {
        int instrSlot = -1, sampleSlot = -1;
        if (!(in >> instrSlot >> sampleSlot))
            return false;
        if (instrSlot >= 0 && instrSlot < kInstrumentSlots && sampleSlot >= 0 && sampleSlot <= 256)
            instrumentSampleSlot[instrSlot] = sampleSlot;
    }
    else if (token == "INSTRUMENT_PARAM")
    {
        int instrSlot = -1;
        std::string paramName;
        double value = 0.0;
        if (!(in >> instrSlot >> paramName >> value))
            return false;
        if (instrSlot >= 0 && instrSlot < kInstrumentSlots)
            instrumentParams[instrSlot][paramName] = value;
    }
    else if (token == "INSTRUMENT_PITCH")
    {
        int instrSlot = -1;
        float semitones = 0.0f;
        if (!(in >> instrSlot >> semitones))
            return false;
        if (instrSlot >= 0 && instrSlot < kInstrumentSlots)
            instrumentPitch[instrSlot] = semitones;
    }
    else if (token == "CHANNEL_VOLUME")
    {
        // Integer percent per channel (0-200, 100 = unity), matching
        // channel_cli.cpp's CLI/GUI display. Converted to a 0.0-2.0 float
        // multiplier for XtpSequencer::setChannelVolumes.
        auto values = readNumericTokenSequence(in);
        channelVolume.assign(values.size(), 1.0f);
        for (std::size_t i = 0; i < values.size(); ++i)
            channelVolume[i] = (float) juce::jlimit(0, 200, (int) values[i]) / 100.0f;
    }
    else if (token == "SAMPLE_BANK")
    {
        int slot = -1;
        std::string path;
        if (!(in >> slot >> std::quoted(path)))
            return false;
        addSampleEntry(slot, juce::File(juce::String(path)).getFileNameWithoutExtension(), path, moduleDirectory);
    }
    else if (token == "SAMPLE_ENTRY")
    {
        int slot = -1;
        std::string name, path;
        if (!(in >> slot >> std::quoted(name) >> std::quoted(path)))
            return false;
        addSampleEntry(slot, juce::String::fromUTF8(name.data(), (int) name.size()), path, moduleDirectory);
    }
    else if (token == "INSTRUMENT_ASSIGN")
    {
        int slot = -1;
        std::string pluginId;
        if (!(in >> slot >> std::quoted(pluginId)))
            return false;
        if (slot >= 0 && slot < (int) instruments.size())
        {
            juce::String id = juce::String::fromUTF8(pluginId.data(), (int) pluginId.size());
            // exTracker bundles soundfonts next to the song and stores the path
            // relative to it ("sf2:song_instruments/instr_003_x.sf2"); a
            // ":melodic:NN" suffix (drum-key chromatic mode) follows the path.
            if (id.startsWith("sf2:"))
            {
                juce::String path = id.substring(4);
                const int melodic = path.lastIndexOf(":melodic:");
                const juce::String suffix = melodic >= 0 ? path.substring(melodic) : juce::String();
                if (melodic >= 0)
                    path = path.substring(0, melodic);
                if (! juce::File::isAbsolutePath(path))
                    path = moduleDirectory.getChildFile(path).getFullPathName();
                id = "sf2:" + path + suffix;
            }
            instruments[(size_t) slot] = id;
        }
    }
    else if (token == "MODULE_MESSAGE")
    {
        std::string escaped;
        if (!(in >> std::quoted(escaped)))
            return false;
        moduleMessage = unescapeModuleMessage(escaped);
    }
    else
    {
        // Unknown token from a newer format version -- best-effort skip so the
        // rest of the file still loads instead of aborting entirely.
        readNumericTokenSequence(in);
    }
    return true;
}

bool Song::loadFromFile(const juce::File& file, juce::String& errorMessage)
{
    *this = Song {};

    std::ifstream in(file.getFullPathName().toStdString(), std::ios::binary);
    if (!in)
    {
        errorMessage = "Could not open file";
        return false;
    }

    std::string magic;
    long long fileRows = 0, fileChannels = 0;
    in >> magic >> fileRows >> fileChannels;
    if (!in || fileRows <= 0 || fileChannels <= 0)
    {
        errorMessage = "Invalid or missing header";
        return false;
    }
    if (fileRows > 100000 || fileChannels > 256)
    {
        errorMessage = "Header dimensions implausible (corrupt file?)";
        return false;
    }

    const bool isSongV1   = (magic == "EXTRACKER_SONG_V1");
    const bool isLegacyV1 = (magic == "EXTRACKER_PATTERN_V1" || magic == "EXTRACKER_MODULE_V1");
    const bool isLegacyV2 = (magic == "EXTRACKER_PATTERN_V2" || magic == "EXTRACKER_MODULE_V2");
    if (!isSongV1 && !isLegacyV1 && !isLegacyV2)
    {
        errorMessage = "Unrecognised magic: " + juce::String(magic);
        return false;
    }

    rows = (int) fileRows;
    channels = (int) fileChannels;

    // Matches exTracker's own runtime default (main.cpp assigns these to fresh
    // instrument slots 0/1 before any file is loaded); a file's own
    // INSTRUMENT_ASSIGN for slot 0/1, if present, overrides this below.
    instruments[0] = "builtin.sine";
    instruments[1] = "builtin.square";

    const juce::File moduleDirectory = file.getParentDirectory();

    if (isSongV1)
    {
        long long filePatternCount = 0, fileSongLength = 0, fileCurrentPattern = 0, fileCurrentSongPosition = 0;
        in >> filePatternCount >> fileSongLength >> fileCurrentPattern >> fileCurrentSongPosition;
        juce::ignoreUnused(fileCurrentPattern, fileCurrentSongPosition);
        if (!in || filePatternCount <= 0 || fileSongLength <= 0)
        {
            errorMessage = "Invalid SONG_V1 header";
            return false;
        }
        if (filePatternCount > 100000)
        {
            errorMessage = "Pattern count implausible (corrupt file?)";
            return false;
        }

        patterns.assign((size_t) filePatternCount, Pattern {});
        for (auto& p : patterns)
        {
            p.rows = rows;
            p.channels = channels;
            p.steps.assign((size_t) (rows * channels), Step {});
        }
        patternSwing.assign((size_t) filePatternCount, 50);

        std::string pendingToken;
        bool hasPendingToken = false;

        for (long long patternIndex = 0; patternIndex < filePatternCount; ++patternIndex)
        {
            std::string patternToken;
            long long storedIndex = -1;
            if (hasPendingToken)
            {
                patternToken = pendingToken;
                hasPendingToken = false;
            }
            else
            {
                in >> patternToken;
            }
            in >> storedIndex;
            if (!in || patternToken != "PATTERN" || storedIndex != patternIndex)
            {
                errorMessage = "Malformed PATTERN marker at index " + juce::String(patternIndex);
                return false;
            }

            Pattern& pat = patterns[(size_t) patternIndex];
            for (;;)
            {
                std::string firstToken;
                if (!(in >> firstToken))
                {
                    if (patternIndex + 1 < filePatternCount)
                    {
                        errorMessage = "Unexpected end of file inside pattern data";
                        return false;
                    }
                    break;
                }

                if (firstToken == "PATTERN" || isKnownTailToken(firstToken))
                {
                    pendingToken = firstToken;
                    hasPendingToken = true;
                    break;
                }

                int parsedRow = 0;
                {
                    std::istringstream rowParser(firstToken);
                    char extra = '\0';
                    if (!(rowParser >> parsedRow) || (rowParser >> extra))
                    {
                        errorMessage = "Malformed cell row index: " + juce::String(firstToken);
                        return false;
                    }
                }

                if (!parseCellLine(in, parsedRow, /*withSample=*/true, pat))
                {
                    errorMessage = "Malformed cell line at row " + juce::String(parsedRow);
                    return false;
                }
            }
        }

        std::string tailToken;
        for (;;)
        {
            if (hasPendingToken)
            {
                tailToken = pendingToken;
                hasPendingToken = false;
            }
            else if (!(in >> tailToken))
            {
                break;
            }

            if (!applyTrailerToken(in, tailToken, moduleDirectory))
            {
                errorMessage = "Malformed trailer token: " + juce::String(tailToken);
                return false;
            }
        }

        if (songOrder.empty())
            for (long long i = 0; i < filePatternCount; ++i)
                songOrder.push_back((int) i);
    }
    else
    {
        patterns.assign(1, Pattern {});
        patterns[0].rows = rows;
        patterns[0].channels = channels;
        patterns[0].steps.assign((size_t) (rows * channels), Step {});
        patternSwing.assign(1, 50);
        songOrder = { 0 };

        const bool withSample = isLegacyV2;
        const long long totalCells = fileRows * fileChannels;
        for (long long i = 0; i < totalCells; ++i)
        {
            int row = 0;
            in >> row;
            if (!in)
            {
                errorMessage = "Unexpected end of file inside legacy pattern data";
                return false;
            }
            if (!parseCellLine(in, row, withSample, patterns[0]))
            {
                errorMessage = "Malformed legacy cell line at row " + juce::String(row);
                return false;
            }
        }

        std::string tailToken;
        while (in >> tailToken)
        {
            if (!applyTrailerToken(in, tailToken, moduleDirectory))
            {
                errorMessage = "Malformed trailer token: " + juce::String(tailToken);
                return false;
            }
        }
    }

    loaded = true;
    return true;
}

std::vector<float> Song::channelGains() const
{
    std::vector<float> gains((size_t) juce::jmax(0, channels), 1.0f);
    for (std::size_t ch = 0; ch < gains.size() && ch < channelVolume.size(); ++ch)
        gains[ch] = channelVolume[ch];
    for (const auto& track : tracks)
        for (int ch = track.first; ch <= track.last && ch < (int) gains.size(); ++ch)
            gains[(size_t) ch] *= track.volume;
    return gains;
}

std::vector<bool> Song::channelSilenced() const
{
    std::vector<bool> silenced((size_t) juce::jmax(0, channels), false);
    for (std::size_t ch = 0; ch < silenced.size() && ch < channelMuted.size(); ++ch)
        silenced[ch] = channelMuted[ch];
    for (const auto& track : tracks)
        if (track.muted)
            for (int ch = track.first; ch <= track.last && ch < (int) silenced.size(); ++ch)
                silenced[(size_t) ch] = true;
    return silenced;
}

double Song::estimateDurationSeconds() const
{
    if (!loaded || songOrder.empty() || patterns.empty())
        return 0.0;

    double total = 0.0;
    const double bpm = juce::jmax(1.0, tempoBpm);
    const int tpb = juce::jmax(1, ticksPerBeat);
    const int tpr = juce::jmax(1, ticksPerRow);
    const double tickSecondsBase = 60.0 / (bpm * (double) tpb);

    for (int patternIndex : songOrder)
    {
        if (patternIndex < 0 || patternIndex >= (int) patterns.size())
            continue;

        const int patRows = patterns[(size_t) patternIndex].rows;
        const int swing = (patternIndex < (int) patternSwing.size()) ? patternSwing[(size_t) patternIndex] : 50;
        const double swingFrac = juce::jlimit(50, 75, swing) / 100.0;
        const double evenFactor = juce::jmax(0.25, 2.0 * (1.0 - swingFrac));
        const double oddFactor  = juce::jmax(0.25, 2.0 * swingFrac);

        for (int row = 0; row < patRows; ++row)
        {
            const double factor = (row % 2 == 1) ? oddFactor : evenFactor;
            total += tickSecondsBase * factor * (double) tpr;
        }
    }

    return total;
}

} // namespace xtp
