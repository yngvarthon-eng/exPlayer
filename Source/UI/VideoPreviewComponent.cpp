#include "VideoPreviewComponent.h"
#include "../Video/VideoFileProbe.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Caps decode resolution so a 4K/8K source doesn't spend needless CPU/memory
    // decoding far more detail than this panel (or even a typical fullscreen
    // window) can show. 1920 lets true 1080p ("Full HD") sources decode at their
    // native resolution instead of being downscaled to 720p; measured decode
    // throughput (10-30x real-time even for 4K/10-bit HEVC, per
    // FfmpegVideoFrameDecoder) leaves plenty of headroom at this size on
    // ordinary desktop hardware.
    constexpr int kMaxDecodeWidth = 1920;
}

VideoPreviewComponent::VideoPreviewComponent(AudioEngine* engine) : audioEngine(engine)
{
    startTimer(40); // 25Hz, same cadence as VisualizationComponent

    audioTrackCombo = std::make_unique<juce::ComboBox>("AudioTrack");
    audioTrackCombo->setVisible(false);
    audioTrackCombo->onChange = [this]
    {
        const int idx = audioTrackCombo->getSelectedItemIndex();
        if (idx >= 0)
            audioEngine->setVideoAudioTrack(idx);
    };
    addAndMakeVisible(*audioTrackCombo);

    subtitleTrackCombo = std::make_unique<juce::ComboBox>("SubtitleTrack");
    subtitleTrackCombo->setVisible(false);
    subtitleTrackCombo->onChange = [this] { onSubtitleTrackSelected(); };
    addAndMakeVisible(*subtitleTrackCombo);
}

VideoPreviewComponent::~VideoPreviewComponent()
{
    stopTimer();

    // Invalidate all pending callAsync lambdas *before* anything is torn down
    // so a lambda queued but not yet executed on the message thread bails out
    // safely without touching any member variables -- same reasoning as
    // AudioEngine::~AudioEngine().
    ++(*probeGeneration);
    ++(*subtitleGeneration);

    if (probeLoader)
        probeLoader->signalThreadShouldExit();
    for (auto& z : zombieProbeLoaders)
        z->signalThreadShouldExit();

    if (probeLoader && probeLoader->isThreadRunning())
        probeLoader->stopThread(12000);
    probeLoader.reset();

    for (auto& z : zombieProbeLoaders)
        z->stopThread(12000);
    zombieProbeLoaders.clear();

    if (subtitleLoader)
        subtitleLoader->signalThreadShouldExit();
    for (auto& z : zombieSubtitleLoaders)
        z->signalThreadShouldExit();

    if (subtitleLoader && subtitleLoader->isThreadRunning())
        subtitleLoader->stopThread(12000);
    subtitleLoader.reset();

    for (auto& z : zombieSubtitleLoaders)
        z->stopThread(12000);
    zombieSubtitleLoaders.clear();
}

void VideoPreviewComponent::setVideoFile(const juce::File& file)
{
    decoder.reset();
    lastDisplayedFrame.reset();
    lastSeekRequestedMs = 0;
    seekInFlight = false;
    lastMasterTime = -1.0;
    lastFrameReceivedMs = 0;
    currentFile = file;
    subtitleCues.clear();

    // Bump the generation so a still-running probe from a previous call is
    // discarded once it completes, then hand its thread off to be reaped --
    // same cancellation pattern as AudioEngine::loadVideoInternal().
    const int generation = ++(*probeGeneration);

    // Also discard any subtitle extraction still running for the file just
    // replaced -- its result would otherwise land on the new file's cue list.
    ++(*subtitleGeneration);
    if (subtitleLoader)
        subtitleLoader->signalThreadShouldExit();
    zombieSubtitleLoaders.erase(
        std::remove_if(zombieSubtitleLoaders.begin(), zombieSubtitleLoaders.end(),
                       [](const auto& t) { return !t->isThreadRunning(); }),
        zombieSubtitleLoaders.end());
    if (subtitleLoader && subtitleLoader->isThreadRunning())
        zombieSubtitleLoaders.push_back(std::move(subtitleLoader));
    else
        subtitleLoader.reset();

    if (probeLoader)
        probeLoader->signalThreadShouldExit();
    zombieProbeLoaders.erase(
        std::remove_if(zombieProbeLoaders.begin(), zombieProbeLoaders.end(),
                       [](const auto& t) { return !t->isThreadRunning(); }),
        zombieProbeLoaders.end());
    if (probeLoader && probeLoader->isThreadRunning())
        zombieProbeLoaders.push_back(std::move(probeLoader));
    else
        probeLoader.reset();

    probeLoader = std::make_unique<ProbeLoader>();

    auto genToken = probeGeneration;
    probeLoader->task = [this, file, generation, genToken]()
    {
        auto probe = VideoFileProbe::probe(file);

        if (genToken->load() != generation)
            return; // a newer load was requested; discard this one

        juce::MessageManager::callAsync([this, file, generation, genToken, probe]()
        {
            if (genToken->load() != generation)
                return; // component destroyed, or a newer load was requested

            applyProbeResult(file, probe);
        });
    };
    probeLoader->startThread();
}

void VideoPreviewComponent::applyProbeResult(const juce::File& file, const VideoProbeResult& probe)
{
    fprintf(stderr, "VideoPreviewComponent: probed '%s' hasVideo=%d hasAudio=%d %dx%d @%.2ffps dur=%.1fs err='%s'\n",
            file.getFileName().toRawUTF8(), probe.hasVideo, probe.hasAudio,
            probe.width, probe.height, probe.fps, probe.durationSeconds,
            probe.errorMessage.toRawUTF8());

    // Populate/hide the audio track picker regardless of whether the file has a
    // usable video stream -- an audio-only "video" playlist entry (e.g. an mkv
    // with no video stream) still benefits from track selection.
    if (probe.audioTracks.size() > 1)
    {
        audioTrackCombo->clear(juce::dontSendNotification);
        for (int i = 0; i < probe.audioTracks.size(); ++i)
            audioTrackCombo->addItem(probe.audioTracks[i], i + 1);
        audioTrackCombo->setSelectedItemIndex(0, juce::dontSendNotification);
        audioTrackCombo->setVisible(true);
    }
    else
    {
        audioTrackCombo->clear(juce::dontSendNotification);
        audioTrackCombo->setVisible(false);
    }

    // Subtitle track picker: an "Off" entry, then each embedded stream, then a
    // same-named sidecar .srt if one sits next to the video -- defaults to Off
    // rather than guessing the user wants captions burned into their view.
    subtitleTrackCombo->clear(juce::dontSendNotification);
    subtitleSelectionForItem.clear();
    auto sidecarSrt = file.withFileExtension("srt");
    const bool hasSidecar = sidecarSrt.existsAsFile();

    if (probe.subtitleTracks.size() > 0 || hasSidecar)
    {
        int id = 1;
        subtitleTrackCombo->addItem("Subtitles: Off", id++);
        subtitleSelectionForItem.push_back(kSubtitleSelectionOff);

        for (int i = 0; i < probe.subtitleTracks.size(); ++i)
        {
            subtitleTrackCombo->addItem(probe.subtitleTracks[i], id++);
            subtitleSelectionForItem.push_back(i);
        }

        if (hasSidecar)
        {
            subtitleTrackCombo->addItem("External (" + sidecarSrt.getFileName() + ")", id++);
            subtitleSelectionForItem.push_back(kSubtitleSelectionSidecar);
        }

        subtitleTrackCombo->setSelectedId(1, juce::dontSendNotification); // Off
        subtitleTrackCombo->setVisible(true);
    }
    else
    {
        subtitleTrackCombo->setVisible(false);
    }

    if (!probe.hasVideo || probe.width <= 0 || probe.height <= 0)
        return; // no usable video stream -- stays blank

    int width = probe.width;
    int height = probe.height;
    if (width > kMaxDecodeWidth)
    {
        height = (int) ((juce::int64) height * kMaxDecodeWidth / width);
        width = kMaxDecodeWidth;
    }
    width &= ~1; height &= ~1; // even dimensions -- friendlier to yuv/rawvideo scalers

    fprintf(stderr, "VideoPreviewComponent: decoding at %dx%d\n", width, height);
    decoder = std::make_unique<FfmpegVideoFrameDecoder>(file, width, height, probe.fps);
    repaint();
}

void VideoPreviewComponent::clearVideo()
{
    // Discard any in-flight probe/subtitle extraction so its callAsync can't
    // repopulate the UI with data for a video that was just cleared.
    ++(*probeGeneration);
    ++(*subtitleGeneration);

    decoder.reset();
    lastDisplayedFrame.reset();
    seekInFlight = false;
    lastMasterTime = -1.0;
    lastFrameReceivedMs = 0;
    currentFile = juce::File();
    subtitleCues.clear();
    audioTrackCombo->clear(juce::dontSendNotification);
    audioTrackCombo->setVisible(false);
    subtitleTrackCombo->clear(juce::dontSendNotification);
    subtitleTrackCombo->setVisible(false);
    repaint();
}

void VideoPreviewComponent::resized()
{
    auto area = getLocalBounds();
    audioTrackCombo->setBounds(area.removeFromTop(26).removeFromRight(240).reduced(4));
    subtitleTrackCombo->setBounds(area.removeFromTop(26).removeFromRight(240).reduced(4));
}

void VideoPreviewComponent::onSubtitleTrackSelected()
{
    subtitleCues.clear();

    // A previous selection's extraction may still be running -- discard its
    // result when it lands so it can't stomp on the choice just made.
    ++(*subtitleGeneration);

    const int idx = subtitleTrackCombo->getSelectedItemIndex();
    if (!juce::isPositiveAndBelow(idx, (int) subtitleSelectionForItem.size()))
    {
        repaint();
        return;
    }

    const int selection = subtitleSelectionForItem[(size_t) idx];
    if (selection == kSubtitleSelectionOff)
    {
        // handled above (subtitleCues already cleared)
        repaint();
        return;
    }

    if (selection == kSubtitleSelectionSidecar)
    {
        // A local .srt read is just a file load, not a process spawn -- fine
        // to do inline.
        subtitleCues = SubtitleTrack::loadSrtFile(currentFile.withFileExtension("srt"));
        repaint();
        return;
    }

    // Embedded track: extractEmbedded() spawns ffmpeg and demuxes the entire
    // file (subtitle packets are spread across its full duration), which can
    // take as long as the file itself to run -- doing that inline is what was
    // freezing the whole app on selection. Run it on a background thread and
    // apply the result via callAsync, same pattern as setVideoFile()'s probe.
    const int generation = subtitleGeneration->load();

    zombieSubtitleLoaders.erase(
        std::remove_if(zombieSubtitleLoaders.begin(), zombieSubtitleLoaders.end(),
                       [](const auto& t) { return !t->isThreadRunning(); }),
        zombieSubtitleLoaders.end());
    if (subtitleLoader && subtitleLoader->isThreadRunning())
        zombieSubtitleLoaders.push_back(std::move(subtitleLoader));
    else
        subtitleLoader.reset();

    subtitleLoader = std::make_unique<SubtitleLoader>();

    auto file = currentFile;
    auto genToken = subtitleGeneration;
    subtitleLoader->task = [this, file, selection, generation, genToken]()
    {
        auto cues = SubtitleTrack::extractEmbedded(file, selection);

        if (genToken->load() != generation)
            return; // a newer selection was made; discard this one

        juce::MessageManager::callAsync([this, generation, genToken, cues]()
        {
            if (genToken->load() != generation)
                return; // component destroyed, or a newer selection was made

            subtitleCues = cues;
            repaint();
        });
    };
    subtitleLoader->startThread();
}

void VideoPreviewComponent::drawSubtitleCue(juce::Graphics& g, const SubtitleCue& cue)
{
    auto bounds = getLocalBounds().toFloat();
    auto textArea = bounds.reduced(bounds.getWidth() * 0.08f, 0.0f)
                          .removeFromBottom(bounds.getHeight() * 0.22f)
                          .toNearestInt();

    juce::Font font(juce::jmax(14.0f, bounds.getHeight() * 0.045f));
    g.setFont(font);

    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(textArea.toFloat(), 4.0f);

    g.setColour(juce::Colours::white);
    g.drawFittedText(cue.text, textArea, juce::Justification::centred, 3);
}

void VideoPreviewComponent::timerCallback()
{
    if (decoder == nullptr)
    {
        // No video stream to drive frame lookup, but an audio-only "video"
        // entry with subtitles still needs its cue overlay kept in sync with
        // playback position.
        if (!subtitleCues.empty())
            repaint();
        return;
    }

    const double masterTime = audioEngine->getCurrentPlaybackTime();

    // A jump far bigger than one 40ms tick could produce under normal forward
    // playback means the transport seeked (or looped/switched tracks) --
    // reseek the decoder immediately in that case, overriding any seek already
    // in flight. Deliberately NOT triggered by "the buffered frame is stale":
    // a heavy source (e.g. 4K/10-bit HEVC) can decode slower than real-time,
    // in which case every buffered frame is always a bit behind -- reseeking
    // that decoder doesn't fix its speed, it just throws away its progress and
    // makes the backlog worse, which is what was causing the video to freeze
    // for good instead of just lagging a little.
    const bool positionJumped = lastMasterTime >= 0.0
                              && std::abs(masterTime - lastMasterTime) > kSeekJumpThresholdSeconds;
    lastMasterTime = masterTime;

    // Always display whatever the newest available frame is, however stale --
    // a picture that's a bit behind and still moving reads far better than a
    // frozen one, and a merely-slow (not stalled) decoder catches up on its
    // own without help.
    const juce::int64 now = juce::Time::currentTimeMillis();
    auto frame = decoder->getFrameNear(masterTime);
    if (frame != nullptr)
    {
        lastDisplayedFrame = frame;
        lastFrameReceivedMs = now;
        seekInFlight = false; // a fresh frame means any pending seek resolved
        repaint();
    }

    if (positionJumped)
    {
        fprintf(stderr, "VideoPreviewComponent: seek detected, resyncing video to t=%.2fs\n", masterTime);
        decoder->seekTo(masterTime);
        lastSeekRequestedMs = now;
        seekInFlight = true;
    }
    else if (frame == nullptr && !seekInFlight
             && audioEngine->isPlaying()
             && (lastFrameReceivedMs == 0 || now - lastFrameReceivedMs > kNoFrameGraceMs))
    {
        // No frame ever received (fresh load), or none for a suspiciously long
        // time despite the decoder supposedly running -- a genuine stall, not
        // just the ring buffer being momentarily empty between deliveries
        // (decode on this codebase measures 10-30x real-time, so a healthy
        // decoder refills within milliseconds; restarting on every such blip,
        // as this used to do, was itself the cause of visible lag -- each
        // unnecessary restart pays real process-spawn + reseek latency that
        // dwarfs the brief gap it was "fixing"). Gated on isPlaying(): while
        // paused or stopped there's no reason to expect new frames, and once
        // playback reaches the very end of the file a seek there legitimately
        // produces zero frames every time (nothing left to decode) -- without
        // this gate that churns an ffmpeg spawn+EOF cycle forever.
        fprintf(stderr, "VideoPreviewComponent: no frame for >%dms, (re)starting decode at t=%.2fs\n",
                kNoFrameGraceMs, masterTime);
        decoder->seekTo(masterTime);
        lastSeekRequestedMs = now;
        seekInFlight = true;
    }
    else if (frame == nullptr && seekInFlight && audioEngine->isPlaying()
             && now - lastSeekRequestedMs > kSeekStallTimeoutMs)
    {
        // A seek was issued but never resolved even after a generous timeout.
        // Same isPlaying() gate as above -- if the transport stopped (e.g. it
        // was a seek to/past end-of-file) while this was in flight, don't keep
        // retrying a target that will only ever produce EOF.
        fprintf(stderr, "VideoPreviewComponent: seek stalled, retrying at t=%.2fs\n", masterTime);
        decoder->seekTo(masterTime);
        lastSeekRequestedMs = now;
    }

    // A repaint above only happens when a fresh video frame arrived; subtitles
    // need to stay in step with playback position every tick regardless, e.g.
    // to clear a cue exactly on time while the video frame is merely holding.
    if (!subtitleCues.empty())
        repaint();
}

void VideoPreviewComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    if (lastDisplayedFrame != nullptr)
        g.drawImage(lastDisplayedFrame->image, getLocalBounds().toFloat(),
                    juce::RectanglePlacement::centred);

    if (!subtitleCues.empty())
        if (auto* cue = SubtitleTrack::findActiveCue(subtitleCues, audioEngine->getCurrentPlaybackTime()))
            drawSubtitleCue(g, *cue);
}
