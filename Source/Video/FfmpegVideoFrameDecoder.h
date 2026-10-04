#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <deque>
#include <memory>

// Decodes a local video file's frames via an ffmpeg subprocess, buffering a
// handful of them ahead so a passive display component (VideoPreviewComponent)
// can pull whatever frame matches the current audio playback position (audio
// is the master clock -- see FfmpegVideoAudioFormatReader).
//
// Not a juce::AudioFormatReader -- this produces images, not samples -- so it's
// a standalone producer/consumer instead: a persistent background thread owns
// the ffmpeg subprocess and feeds a small ring buffer; callers only ever peek
// the buffer or request a reseek, never block.
class FfmpegVideoFrameDecoder
{
public:
    struct Frame
    {
        juce::Image image;
        double timestampSeconds = 0.0;
    };

    // width/height/fps normally come from a VideoFileProbe result. width/height
    // are the fixed decode resolution ffmpeg is asked to scale to; fps is used
    // only to timestamp frames (see the approximate-timestamp note below).
    FfmpegVideoFrameDecoder(juce::File videoFile, int width, int height, double fps);
    ~FfmpegVideoFrameDecoder();

    // Requests decoding to (re)start at `seconds`. Cheap and safe to call
    // repeatedly/rapidly -- it only updates a pending-seek request read by the
    // one persistent worker thread; the underlying ffmpeg process is killed and
    // relaunched by that thread, not here.
    void seekTo(double seconds);

    // Thread-safe, never blocks. Returns the most recent buffered frame whose
    // timestamp is <= targetSeconds, discarding older ones as a side effect, or
    // nullptr if nothing suitable is buffered yet (e.g. immediately after a seek).
    std::shared_ptr<const Frame> getFrameNear(double targetSeconds);

private:
    struct DecodeThread : public juce::Thread
    {
        explicit DecodeThread(FfmpegVideoFrameDecoder& o) : juce::Thread("video-frame-decoder"), owner(o) {}
        void run() override { owner.runDecodeLoop(*this); }
        FfmpegVideoFrameDecoder& owner;
    };

    void runDecodeLoop(juce::Thread& thread);
    void killProcess();

    juce::File file;
    int width, height;
    double fps;

    std::unique_ptr<juce::ChildProcess> process;
    double seekBaseSeconds = 0.0;
    juce::int64 frameIndexSinceSeek = 0;

    juce::CriticalSection seekLock;
    bool hasPendingSeek = true;   // implicit "seek to 0" kicks off decode on start
    double pendingSeekSeconds = 0.0;

    juce::CriticalSection bufferLock;
    std::deque<std::shared_ptr<Frame>> ringBuffer;
    // Measured decode throughput is typically 10-30x real-time even for heavy
    // 4K/10-bit sources, so this is generous headroom against ordinary system
    // timing jitter (a delayed display tick, a scheduling hiccup) rather than
    // a buffer decode is expected to fill slowly -- draining to empty from
    // jitter alone used to trigger an unnecessary decoder restart in
    // VideoPreviewComponent, which was the actual cause of visible lag.
    // Kept at 12 (not the wider margin a smaller decode size could afford) now
    // that VideoPreviewComponent's decode cap reaches 1920px wide: each buffered
    // ARGB frame there is ~8MB, so this is already ~100MB of frames in flight at
    // 1080p -- doubling it to the old headroom would roughly double that again
    // for no benefit a 10-30x-real-time decoder actually needs.
    static constexpr int kBufferFrames = 12;

    std::unique_ptr<DecodeThread> decodeThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FfmpegVideoFrameDecoder)
};
