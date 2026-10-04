#pragma once

#include <juce_core/juce_core.h>

// Probes a local media file's container info by parsing ffmpeg's own stderr
// banner (ffmpeg -i <file> with no output specified) -- no ffprobe dependency,
// since only ffmpeg itself is assumed to be on PATH elsewhere in this codebase
// (see Export/VideoExporter.cpp, ScreenRecorder.cpp).
struct VideoProbeResult
{
    bool hasVideo = false;
    bool hasAudio = false;
    int width = 0;
    int height = 0;
    double fps = 25.0;           // fallback when unparseable (e.g. true VFR sources)
    double durationSeconds = 0.0;
    juce::String errorMessage;   // non-empty only when probing failed outright

    // One label per audio stream, in the order ffmpeg enumerates them within the
    // file -- i.e. audioTracks[N] is exactly what "-map 0:a:N" addresses, so this
    // list can be handed straight to a UI picker with no extra index translation.
    juce::StringArray audioTracks;

    // Same idea for subtitle streams: subtitleTracks[N] is what "-map 0:s:N"
    // addresses (see SubtitleTrack::extractEmbedded). Text-based formats
    // (SRT/ASS) extract cleanly; bitmap formats (PGS/VobSub) will list here but
    // ffmpeg can't convert them to SRT text, so extraction for those fails.
    juce::StringArray subtitleTracks;
};

namespace VideoFileProbe
{
    // Blocking. Runs ffmpeg with no output file, which always exits non-zero
    // after printing the full input banner to stderr -- exactly what's parsed
    // here. Fast for local files (no network I/O), so no timeout handling.
    VideoProbeResult probe(const juce::File& file);
}
