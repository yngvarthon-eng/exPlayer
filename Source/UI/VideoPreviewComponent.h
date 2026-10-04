#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Video/FfmpegVideoFrameDecoder.h"
#include "../Video/SubtitleTrack.h"
#include "../Video/VideoFileProbe.h"
#include <atomic>
#include <memory>
#include <vector>

// Passive video display: polls AudioEngine's playback position (the master
// clock -- see FfmpegVideoAudioFormatReader/FfmpegVideoFrameDecoder) and draws
// whichever decoded frame matches it. Never drives playback itself, so the
// existing transport controls (PlayerControlsComponent) work unmodified.
//
// Mirrors VisualizationComponent's Timer+paint pattern; also used as the
// reparented child of FullScreenHostWindow for the fullscreen video mode
// (same instance, not a second one -- decode state must not be duplicated).
class VideoPreviewComponent : public juce::Component,
                              public juce::Timer
{
public:
    explicit VideoPreviewComponent(AudioEngine* engine);
    ~VideoPreviewComponent() override;

    // Probes the file and (re)starts a decoder for it. Independent of
    // AudioEngine::loadVideo()'s own probe -- keeps this component decoupled
    // from AudioEngine's internal reader, the same way VisualizationComponent
    // only ever touches audioEngine->getAudioBuffer().
    void setVideoFile(const juce::File& file);
    void clearVideo();

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    AudioEngine* audioEngine;
    std::unique_ptr<FfmpegVideoFrameDecoder> decoder;

    // Only shown when the loaded file has more than one audio stream. Items are
    // 1-based combo box IDs mapping straight to the 0-based ordinal AudioEngine
    // expects (see AudioEngine::setVideoAudioTrack).
    std::unique_ptr<juce::ComboBox> audioTrackCombo;

    // Subtitles: extracted once per selection (see onSubtitleTrackSelected())
    // into an in-memory cue list looked up every paint() against the same
    // AudioEngine playback position used for video frame lookup -- see
    // SubtitleTrack.h for why this is a plain cue list rather than a decoder.
    juce::File currentFile; // needed to re-extract when the subtitle track changes
    std::vector<SubtitleCue> subtitleCues;
    std::unique_ptr<juce::ComboBox> subtitleTrackCombo;
    // Parallel to subtitleTrackCombo's items (index = combo item index, not
    // id): kSubtitleSelectionOff, kSubtitleSelectionSidecar, or an embedded
    // track's "-map 0:s:N" ordinal.
    std::vector<int> subtitleSelectionForItem;
    static constexpr int kSubtitleSelectionOff     = -2;
    static constexpr int kSubtitleSelectionSidecar = -1;

    // Background probe loader: VideoFileProbe::probe() spawns ffmpeg and blocks
    // on its stderr, which is fast but not instant -- run it off the message
    // thread so selecting a video in the playlist can't stall the whole UI
    // (same rationale/pattern as AudioEngine::loadVideoInternal()'s own probe).
    struct ProbeLoader : public juce::Thread
    {
        ProbeLoader() : juce::Thread("video-preview-probe") {}
        std::function<void()> task;
        void run() override { if (task) task(); }
    };
    std::unique_ptr<ProbeLoader> probeLoader;
    // Old loaders whose ffmpeg spawn hasn't returned yet. Kept alive so ~Thread
    // doesn't assert, then reaped once they finish -- same pattern as
    // AudioEngine::zombieLoaders.
    std::vector<std::unique_ptr<ProbeLoader>> zombieProbeLoaders;
    // Shared so callAsync lambdas can safely check it after this component is
    // destroyed; bumped on every setVideoFile() call and in the destructor so
    // a stale probe's result is discarded instead of applied.
    std::shared_ptr<std::atomic<int>> probeGeneration { std::make_shared<std::atomic<int>>(0) };

    // Background subtitle extraction: SubtitleTrack::extractEmbedded() spawns
    // ffmpeg and demuxes the whole file (subtitle packets span its full
    // duration, unlike the "-i" banner probe above), which can take as long as
    // the file itself for anything beyond a short clip -- run it off the
    // message thread so picking a track from the combo box can't freeze the
    // UI. Same generation/zombie-reaping pattern as ProbeLoader above.
    struct SubtitleLoader : public juce::Thread
    {
        SubtitleLoader() : juce::Thread("video-preview-subtitle") {}
        std::function<void()> task;
        void run() override { if (task) task(); }
    };
    std::unique_ptr<SubtitleLoader> subtitleLoader;
    std::vector<std::unique_ptr<SubtitleLoader>> zombieSubtitleLoaders;
    std::shared_ptr<std::atomic<int>> subtitleGeneration { std::make_shared<std::atomic<int>>(0) };

    void applyProbeResult(const juce::File& file, const VideoProbeResult& probe);
    void onSubtitleTrackSelected();
    void drawSubtitleCue(juce::Graphics& g, const SubtitleCue& cue);
    std::shared_ptr<const FfmpegVideoFrameDecoder::Frame> lastDisplayedFrame;
    juce::int64 lastSeekRequestedMs = 0;

    // -1 sentinel = no previous tick yet (just loaded/seeked). Used to detect
    // an explicit seek by a position jump far bigger than one timer tick could
    // produce under normal forward playback -- see timerCallback()'s comment
    // for why this, not "how stale is the buffered frame", is what decides
    // whether to reseek the decoder.
    double lastMasterTime = -1.0;

    // True from the moment a seekTo() is issued until a frame actually arrives.
    // While true, no further seekTo() is issued except via the stall timeout
    // below -- a heavy source (e.g. 4K/10-bit HEVC) can take seconds to produce
    // its first frame after a relaunch, and reseeking again before that happens
    // just kills the process before it ever gets a chance, which is a relaunch
    // storm that never converges.
    bool seekInFlight = false;

    // Wall-clock time a frame was last successfully received, or 0 if none
    // ever has been (fresh load/seek). Used to tell "the ring buffer is
    // transiently empty because decode is a healthy source that's merely
    // between deliveries" (normal, self-resolving -- decode on this codebase
    // has measured 10-30x real-time, so gaps are timing jitter, not a stalled
    // decoder) apart from "the decoder is actually dead and needs kicking".
    // Restarting on every momentary empty buffer was itself the cause of
    // visible playback lag: each unnecessary restart pays real process-spawn +
    // re-seek latency for no reason, even though raw decode speed was never
    // the bottleneck.
    juce::int64 lastFrameReceivedMs = 0;

    // A position jump bigger than this between two ~40ms-apart ticks is
    // treated as an explicit seek (dragging the transport slider, switching
    // tracks); normal playback advances far less than this per tick even under
    // heavy system load.
    static constexpr double kSeekJumpThresholdSeconds = 1.0;
    // How long the frame buffer may sit empty before that's treated as a
    // genuine stall worth restarting the decoder for, rather than just normal
    // jitter to wait out.
    static constexpr int kNoFrameGraceMs = 1500;
    // Only reseeks due to having no frame at all while one is already in
    // flight if it's been stuck this long -- a genuine stall/failure, not just
    // a slow decode warming up.
    static constexpr int kSeekStallTimeoutMs = 8000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoPreviewComponent)
};
