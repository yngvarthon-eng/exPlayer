#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>

// Decodes the audio track of a local video (or any ffmpeg-readable media) file
// via an ffmpeg subprocess, with real seek support and a real finite duration --
// unlike FfmpegAudioFormatReader (used for internet radio), which deliberately
// has neither since a live stream has no seek concept and no known length.
//
// Kept as a separate class rather than folding into FfmpegAudioFormatReader:
// the two have opposite invariants (infinite/non-seekable vs finite/seekable),
// and different ffmpeg flags (-reconnect/-rw_timeout for network vs -ss for
// local seeking), so sharing one class would mostly add branching rather than
// remove duplication. What IS reused verbatim from that class: the
// juce::ChildProcess + argv spawn pattern, the int16->int32 conversion loop,
// and the destructor's kill()+waitForProcessToFinish() zombie reap.
//
// Always decodes to a fixed 48kHz/stereo/16-bit PCM format, matching
// FfmpegAudioFormatReader, so AudioEngine's video and radio paths share the
// same downstream assumptions.
class FfmpegVideoAudioFormatReader : public juce::AudioFormatReader
{
public:
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kChannels = 2;

    // durationSeconds is supplied by the caller (from a VideoFileProbe result)
    // rather than probed here, since AudioEngine::loadVideo() already probes
    // the file once for its own purposes.
    //
    // audioTrackIndex selects which of the file's audio streams to decode, as an
    // ordinal among audio streams (matching VideoProbeResult::audioTracks and
    // ffmpeg's own "-map 0:a:N" addressing) -- not a raw container stream index.
    // -1 lets ffmpeg pick its own default stream, same as before this existed.
    static std::unique_ptr<FfmpegVideoAudioFormatReader> createForFile(
        const juce::File& file, double durationSeconds, int audioTrackIndex,
        juce::String& errorMessage);

    ~FfmpegVideoAudioFormatReader() override;

    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
                      juce::int64 startSampleInFile, int numSamples) override;

private:
    FfmpegVideoAudioFormatReader(juce::File f, std::unique_ptr<juce::ChildProcess> p,
                                 double durationSeconds, int audioTrackIndex);

    // Kills+reaps the current process, relaunches ffmpeg with -ss targeting
    // targetSample, and resets read state to continue from there. Returns false
    // if the relaunch fails to start (readSamples() then emits silence rather
    // than leaving the audio thread stuck).
    bool relaunchAt(juce::int64 targetSample);

    juce::File file;
    int audioTrackIndex = -1;
    std::unique_ptr<juce::ChildProcess> process;
    juce::MemoryBlock pendingBytes;
    bool endOfStream = false;
    juce::int64 expectedNextSample = 0;

    // Tolerance around expectedNextSample before a readSamples() request is
    // treated as a seek rather than the transport's normal sequential read.
    static constexpr juce::int64 kSeekToleranceSamples = 4800; // 0.1s @ 48kHz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FfmpegVideoAudioFormatReader)
};
