#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// A single timed subtitle cue, as parsed from SRT text.
struct SubtitleCue
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    juce::String text; // may contain '\n' for multi-line cues
};

// Whole-track subtitle support: extract once (either from an embedded stream
// via ffmpeg, or a plain sidecar .srt file) and hold the result as an in-memory
// cue list looked up against playback position -- the same "audio position is
// the master clock" model VideoPreviewComponent already uses for video frames,
// just against a small list instead of a live decoder. Deliberately SRT-only:
// full ASS/SSA styling and effects are out of scope (see FfmpegVideoFrameDecoder
// if burned-in libass rendering is ever wanted instead).
namespace SubtitleTrack
{
    // Parses SRT-formatted text into cues, in ascending start-time order.
    // Tolerant of minor real-world formatting variance (CRLF line endings,
    // missing/non-numeric index lines, a stray malformed block) since .srt
    // files in the wild are inconsistent -- a bad block is skipped rather than
    // aborting the whole parse.
    std::vector<SubtitleCue> parseSrt(const juce::String& srtText);

    // Extracts the given subtitle stream -- an ordinal among the file's
    // subtitle streams, matching ffmpeg's own "-map 0:s:N" addressing (and
    // VideoProbeResult::subtitleTracks) -- as SRT text via one blocking ffmpeg
    // call, then parses it. Returns an empty vector on failure (no subtitle
    // stream at that index, or a codec ffmpeg can't convert to SRT, e.g. a
    // bitmap format like PGS/VobSub).
    std::vector<SubtitleCue> extractEmbedded(const juce::File& videoFile, int subtitleTrackIndex);

    // Reads and parses a standalone .srt file (e.g. a sidecar next to a video).
    // Tries UTF-8 first and falls back to Latin-1 on invalid byte sequences --
    // most non-UTF-8 subtitle files in the wild are Latin-1, so that's the one
    // fallback attempted rather than guessing a specific code page.
    std::vector<SubtitleCue> loadSrtFile(const juce::File& srtFile);

    // Binary-searches `cues` (assumed sorted by startSeconds, as parseSrt
    // produces) for whichever one is active at `seconds`, or nullptr if none
    // is -- cheap enough to call every UI timer tick even for a long track.
    const SubtitleCue* findActiveCue(const std::vector<SubtitleCue>& cues, double seconds);
}
