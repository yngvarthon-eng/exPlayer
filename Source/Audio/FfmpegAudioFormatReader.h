#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>

// Decodes an internet radio stream via an ffmpeg subprocess -- used as a fallback
// by AudioEngine::loadStream() for formats JUCE's AudioFormatManager has no native
// Linux decoder for (MP3, AAC-LC, HE-AAC v1/v2), plus anything else ffmpeg itself
// understands.
//
// Always decodes to a fixed 48kHz/stereo/16-bit PCM format regardless of the
// source's own rate/channel count -- radio doesn't need bit-exact passthrough,
// and fixing the format means ffmpeg's output can be read without probing it
// first, the same way the rest of this class works.
class FfmpegAudioFormatReader : public juce::AudioFormatReader
{
public:
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kChannels = 2;

    // Starts ffmpeg decoding the given URL and blocks briefly (bounded by ffmpeg's
    // own connect timeout, set below) to confirm it actually produces audio before
    // returning a reader. Returns nullptr and fills errorMessage on failure.
    static std::unique_ptr<FfmpegAudioFormatReader> createForUrl(const juce::String& url,
                                                                  juce::String& errorMessage);

    ~FfmpegAudioFormatReader() override;

    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
                      juce::int64 startSampleInFile, int numSamples) override;

private:
    explicit FfmpegAudioFormatReader(std::unique_ptr<juce::ChildProcess> ffmpegProcess);

    std::unique_ptr<juce::ChildProcess> process;
    juce::MemoryBlock pendingBytes; // bytes already read from the process during validation
    bool endOfStream = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FfmpegAudioFormatReader)
};
