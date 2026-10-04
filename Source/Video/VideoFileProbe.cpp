#include "VideoFileProbe.h"

namespace
{
    double parseDuration(const juce::String& banner)
    {
        auto afterLabel = banner.fromFirstOccurrenceOf("Duration: ", false, false);
        if (afterLabel.isEmpty())
            return 0.0;

        auto durStr = afterLabel.upToFirstOccurrenceOf(",", false, false).trim(); // "00:00:05.00"
        auto parts = juce::StringArray::fromTokens(durStr, ":", "");
        if (parts.size() != 3)
            return 0.0;

        return parts[0].getDoubleValue() * 3600.0
             + parts[1].getDoubleValue() * 60.0
             + parts[2].getDoubleValue();
    }

    bool parseResolution(const juce::String& videoLine, int& width, int& height)
    {
        for (auto& token : juce::StringArray::fromTokens(videoLine, " ,", ""))
        {
            auto t = token.trim();

            // Codec fourCCs are commonly rendered as hex literals like
            // "0x31637661" earlier in the same line (e.g. "avc1 / 0x31637661") --
            // these also match "digitsx digits" and must not be mistaken for a
            // resolution. A real resolution's width is never "0".
            if (t.startsWithIgnoreCase("0x"))
                continue;

            auto xPos = t.indexOfChar('x');
            if (xPos <= 0 || xPos >= t.length() - 1)
                continue;

            auto w = t.substring(0, xPos);
            auto h = t.substring(xPos + 1);

            // Strip any trailing non-digit junk from h (defensive -- the space/comma
            // tokenizer above already isolates "WxH" on its own in practice).
            int hEnd = 0;
            while (hEnd < h.length() && juce::CharacterFunctions::isDigit(h[hEnd]))
                ++hEnd;
            h = h.substring(0, hEnd);

            if (w.isNotEmpty() && h.isNotEmpty()
                && w.containsOnly("0123456789") && h.containsOnly("0123456789"))
            {
                width = w.getIntValue();
                height = h.getIntValue();
                return true;
            }
        }
        return false;
    }

    // Pulls the language tag out of a stream header line, e.g.
    // "  Stream #0:1(eng): Audio: aac (LC), 48000 Hz, stereo, fltp, 127 kb/s" -> "eng".
    // Returns empty when the source has no language tag (common for e.g. AVI/MP4
    // muxed without one) rather than guessing.
    juce::String parseStreamLanguage(const juce::String& line)
    {
        auto afterHash = line.fromFirstOccurrenceOf("Stream #", false, false);
        auto openParen = afterHash.indexOfChar('(');
        auto closeParen = afterHash.indexOfChar(')');
        if (openParen > 0 && closeParen > openParen)
            return afterHash.substring(openParen + 1, closeParen);
        return {};
    }

    // "  ...: Audio: aac (LC), 48000 Hz, stereo, fltp, 127 kb/s" -> "aac (LC)"
    juce::String parseAudioCodec(const juce::String& line)
    {
        return line.fromFirstOccurrenceOf("Audio: ", false, false)
                   .upToFirstOccurrenceOf(",", false, false)
                   .trim();
    }

    double parseFps(const juce::String& videoLine)
    {
        auto tokens = juce::StringArray::fromTokens(videoLine, ",", "");

        for (auto& token : tokens)
            if (token.trim().endsWith(" fps"))
                return token.trim().dropLastCharacters(4).getDoubleValue();

        // No decoded frame rate reported (e.g. VFR sources) -- fall back to the
        // container's time base, which is always present.
        for (auto& token : tokens)
            if (token.trim().endsWith(" tbr"))
                return token.trim().dropLastCharacters(4).getDoubleValue();

        return 25.0;
    }
}

VideoProbeResult VideoFileProbe::probe(const juce::File& file)
{
    VideoProbeResult result;

    // -nostdin: without it, ffmpeg calls tcsetattr() on its inherited stdin to
    // poll for interactive keypresses. JUCE's ChildProcess never redirects fd 0
    // and never puts the child in its own process group, so if this app isn't
    // the terminal's foreground process group at that moment, tcsetattr()
    // raises SIGTTOU for the whole shared process group -- stopping both
    // ffmpeg *and* every thread of this app, not just the child. Every other
    // ffmpeg spawn in this codebase already carries this flag; this one was
    // the sole omission and could freeze the entire app solid.
    juce::StringArray args { "ffmpeg", "-hide_banner", "-nostdin", "-i", file.getFullPathName() };

    juce::ChildProcess process;
    if (!process.start(args, juce::ChildProcess::wantStdErr))
    {
        result.errorMessage = "Could not start ffmpeg to probe '" + file.getFileName() + "'.";
        return result;
    }

    // ffmpeg always exits non-zero here (no output file was given) after printing
    // the full input banner to stderr -- readAllProcessOutput() blocks until it
    // exits, which is fast for a local file (no network I/O, no encoding).
    juce::String banner = process.readAllProcessOutput();

    if (banner.isEmpty())
    {
        result.errorMessage = "ffmpeg produced no output probing '" + file.getFileName() + "'.";
        return result;
    }

    result.durationSeconds = parseDuration(banner);

    for (auto& line : juce::StringArray::fromLines(banner))
    {
        if (line.contains("Video:"))
        {
            result.hasVideo = true;
            parseResolution(line, result.width, result.height);
            result.fps = parseFps(line);
        }
        else if (line.contains("Audio:"))
        {
            result.hasAudio = true;

            juce::String label = "Track " + juce::String(result.audioTracks.size() + 1);
            auto lang = parseStreamLanguage(line);
            if (lang.isNotEmpty())
                label += " (" + lang + ")";
            auto codec = parseAudioCodec(line);
            if (codec.isNotEmpty())
                label += " - " + codec;
            result.audioTracks.add(label);
        }
        else if (line.contains("Subtitle:"))
        {
            juce::String label = "Subtitles " + juce::String(result.subtitleTracks.size() + 1);
            auto lang = parseStreamLanguage(line);
            if (lang.isNotEmpty())
                label += " (" + lang + ")";
            result.subtitleTracks.add(label);
        }
    }

    if (!result.hasVideo && !result.hasAudio)
        result.errorMessage = "ffmpeg could not identify any audio or video stream in '"
                             + file.getFileName() + "'.";

    return result;
}
