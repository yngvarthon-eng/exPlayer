#include "SubtitleTrack.h"
#include "../Export/VideoExporter.h"

namespace
{
    // "00:01:23,456" or "00:01:23.456" (some malformed files use '.') -> seconds.
    // Returns -1.0 if the string doesn't look like a timestamp at all.
    double parseSrtTimestamp(const juce::String& raw)
    {
        auto t = raw.trim();
        auto sepPos = juce::jmax(t.lastIndexOfChar(','), t.lastIndexOfChar('.'));
        if (sepPos <= 0)
            return -1.0;

        auto hms = juce::StringArray::fromTokens(t.substring(0, sepPos), ":", "");
        if (hms.size() != 3)
            return -1.0;

        const double ms = t.substring(sepPos + 1).getDoubleValue();
        return hms[0].getDoubleValue() * 3600.0
             + hms[1].getDoubleValue() * 60.0
             + hms[2].getDoubleValue()
             + ms / 1000.0;
    }

    // Strips "<...>" (e.g. "<i>", "<font color=...>") and "{...}" (stray ASS
    // override codes some tools leave in when converting to SRT) so cue text
    // renders as plain text instead of literal markup.
    juce::String stripMarkup(const juce::String& line)
    {
        juce::String out;
        bool inTag = false;
        for (int i = 0; i < line.length(); ++i)
        {
            auto c = line[i];
            if (c == '<' || c == '{') { inTag = true; continue; }
            if (c == '>' || c == '}') { inTag = false; continue; }
            if (!inTag)
                out += juce::String::charToString(c);
        }
        return out;
    }

    // Minimal well-formed-UTF-8 check (no external dependency, no reliance on
    // how any particular JUCE version's fromUTF8() handles invalid bytes) --
    // just enough to decide UTF-8 vs. the Latin-1 fallback below.
    bool isValidUtf8(const void* data, int size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        int i = 0;
        while (i < size)
        {
            const unsigned char b = bytes[i];
            int extra;
            if ((b & 0x80) == 0x00)      extra = 0;
            else if ((b & 0xE0) == 0xC0) extra = 1;
            else if ((b & 0xF0) == 0xE0) extra = 2;
            else if ((b & 0xF8) == 0xF0) extra = 3;
            else return false;

            if (i + extra >= size)
                return false;

            for (int k = 1; k <= extra; ++k)
                if ((bytes[i + k] & 0xC0) != 0x80)
                    return false;

            i += extra + 1;
        }
        return true;
    }
}

std::vector<SubtitleCue> SubtitleTrack::parseSrt(const juce::String& srtText)
{
    std::vector<SubtitleCue> cues;

    auto lines = juce::StringArray::fromLines(srtText.replace("\r\n", "\n").replace("\r", "\n"));

    int i = 0;
    while (i < lines.size())
    {
        while (i < lines.size() && lines[i].trim().isEmpty())
            ++i;
        if (i >= lines.size())
            break;

        // Optional numeric cue-index line -- skip it if present.
        if (lines[i].trim().containsOnly("0123456789"))
            ++i;
        if (i >= lines.size())
            break;

        auto arrowPos = lines[i].indexOf("-->");
        if (arrowPos < 0)
        {
            ++i; // not a timestamp line -- malformed block, resync on next blank line
            continue;
        }

        const double start = parseSrtTimestamp(lines[i].substring(0, arrowPos));
        const double end   = parseSrtTimestamp(lines[i].substring(arrowPos + 3));
        ++i;

        juce::StringArray textLines;
        while (i < lines.size() && lines[i].trim().isNotEmpty())
        {
            textLines.add(stripMarkup(lines[i]));
            ++i;
        }

        if (start >= 0.0 && end > start)
            cues.push_back({ start, end, textLines.joinIntoString("\n") });
    }

    return cues;
}

std::vector<SubtitleCue> SubtitleTrack::extractEmbedded(const juce::File& videoFile, int subtitleTrackIndex)
{
    if (!VideoExporter::isFfmpegAvailable() || subtitleTrackIndex < 0)
        return {};

    juce::StringArray args {
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
        "-i", videoFile.getFullPathName(),
        "-map", "0:s:" + juce::String(subtitleTrackIndex),
        "-f", "srt", "-"
    };

    juce::ChildProcess process;
    if (!process.start(args, juce::ChildProcess::wantStdOut))
        return {};

    // Subtitle streams are tiny (plain text, no video/audio decode involved)
    // even for a feature-length file, so blocking here briefly on the message
    // thread is the same accepted tradeoff VideoFileProbe::probe() already
    // makes for local files -- fast enough not to need a background thread.
    auto srtText = process.readAllProcessOutput();
    process.waitForProcessToFinish(5000);

    return parseSrt(srtText);
}

std::vector<SubtitleCue> SubtitleTrack::loadSrtFile(const juce::File& srtFile)
{
    juce::MemoryBlock raw;
    if (!srtFile.loadFileAsData(raw) || raw.getSize() == 0)
        return {};

    const auto* data = raw.getData();
    const auto size = (int) raw.getSize();

    juce::String text;
    if (isValidUtf8(data, size))
    {
        text = juce::String::fromUTF8(static_cast<const char*>(data), size);
    }
    else
    {
        juce::String latin1;
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (int i = 0; i < size; ++i)
            latin1 += juce::String::charToString((juce::juce_wchar) bytes[i]);
        text = latin1;
    }

    return parseSrt(text);
}

const SubtitleCue* SubtitleTrack::findActiveCue(const std::vector<SubtitleCue>& cues, double seconds)
{
    if (cues.empty())
        return nullptr;

    // Upper-bound binary search: find the first cue starting AFTER `seconds`,
    // then step back one to the last cue starting at-or-before it.
    size_t lo = 0, hi = cues.size();
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (cues[mid].startSeconds <= seconds)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo == 0)
        return nullptr;

    const auto& candidate = cues[lo - 1];
    return (seconds < candidate.endSeconds) ? &candidate : nullptr;
}
