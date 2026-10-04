#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>
#include "VideoExporter.h"
#include "../Playlist/PlaylistItem.h"

// Renders an entire playlist into a single video by running an independent VideoExporter
// per track (into its own temp file) and stitching the results together with ffmpeg.
// Reuses VideoExporter completely unchanged for the actual per-track decode/render work --
// only the multi-track sequencing and the final stitch step are new.
//
// Same polling shape as VideoExporter: runs on a background juce::Thread, the caller
// polls getProgress()/hasFinished() from a timer.
class PlaylistVideoExporter : public juce::Thread
{
public:
    enum class TransitionMode { HardCut, Crossfade, Silence };

    struct Settings
    {
        bool showTrackTitles = true;
        TransitionMode transition = TransitionMode::HardCut;
        double transitionSeconds = 1.0; // ignored when transition == HardCut
    };

    struct TrackSpec
    {
        juce::File file;
        bool isXtpSequence = false;
        juce::String name;
        double estimatedDuration = 0.0; // from PlaylistItem::duration; 0 if unknown
    };

    struct SkippedTrack
    {
        juce::String name;
        juce::String reason;
    };

    // Decides which playlist items can be exported, and why the rest can't. `mode`
    // matters only in that PatternGrid/Combined additionally require pattern data (a
    // tracker module or .xtp/.xtd song) -- Visualization mode accepts anything with a
    // local, non-video file. Streams and video-container playlist entries are always
    // excluded. A free function taking plain PlaylistItem data (no PlaylistManager
    // dependency) so it stays easy to unit test.
    static void selectTracks(const juce::Array<PlaylistItem>& items, VideoExporter::ExportMode mode,
                             std::vector<TrackSpec>& outTracks, std::vector<SkippedTrack>& outSkipped);

    // `optionsTemplate` carries every render setting shared across the whole playlist
    // (mode/layout/theme/resolution/text layers/EQ snapshot/pluginFactory); its
    // sourceFile/isXtpSequence/outputFile/trackTitleOverlay are overwritten per track.
    PlaylistVideoExporter(std::vector<TrackSpec> tracks, VideoExporter::Options optionsTemplate,
                          Settings settings, juce::File outputFile);
    ~PlaylistVideoExporter() override;

    void startExport() { startThread(); }
    void cancel() { signalThreadShouldExit(); }

    bool hasFinished() const { return finished.load(); }
    double getProgress() const { return progress.load(); }

    // "Rendering track 3 of 12: <name>" / "Combining tracks..." for the progress window.
    juce::String getCurrentTrackLabel() const;

    VideoExporter::Result getResult() const { return result; }
    const juce::String& getResultMessage() const { return resultMessage; }

private:
    void run() override;
    void finishWith(VideoExporter::Result r, const juce::String& message);

    // Runs one track's VideoExporter to completion, polling it from this thread and
    // folding its progress into the overall combined progress (elapsedWeight/trackWeight/
    // totalWeight are in the same, possibly duration-based, units -- see run()). Returns
    // false (with `finished`/`result` already set) on failure or cancellation; on success,
    // fills `outRenderedDuration` from the finished exporter's getRenderedDurationSeconds().
    bool renderTrack(const TrackSpec& track, const juce::File& outFile,
                     double elapsedWeight, double totalWeight, double trackWeight,
                     double& outRenderedDuration);

    bool stitchHardCut(const std::vector<juce::File>& trackFiles, const juce::File& out, juce::String& error);
    bool stitchSilence(const std::vector<juce::File>& trackFiles, const juce::File& out, juce::String& error);
    bool stitchCrossfade(const std::vector<juce::File>& trackFiles, const std::vector<double>& durations,
                         const juce::File& out, juce::String& error);

    std::vector<TrackSpec> tracks;
    VideoExporter::Options optionsTemplate;
    Settings settings;
    juce::File outputFile;

    std::atomic<double> progress { 0.0 };
    std::atomic<bool> finished { false };
    VideoExporter::Result result = VideoExporter::Result::Failed;
    juce::String resultMessage;

    std::atomic<int> currentTrackIndex { 0 };
    std::atomic<bool> inStitchPhase { false };
    juce::CriticalSection labelLock;
    juce::String currentTrackNameForLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistVideoExporter)
};
