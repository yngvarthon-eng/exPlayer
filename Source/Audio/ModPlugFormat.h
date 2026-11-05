#pragma once

#if defined(SOUNDPLAYER_OPENMPT_SUPPORT) || defined(SOUNDPLAYER_XMP_SUPPORT)

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#ifdef SOUNDPLAYER_OPENMPT_SUPPORT
#include <libopenmpt/libopenmpt.hpp>

/**
 * Custom AudioFormatReader that decodes module files (MOD/XM/IT/S3M/MPTM/etc.)
 * via libopenmpt, providing accurate playback with full OpenMPT-extension support.
 */
class OpenMptAudioFormatReader : public juce::AudioFormatReader
{
public:
    OpenMptAudioFormatReader(juce::InputStream* stream, const juce::String& formatName);
    ~OpenMptAudioFormatReader() override;

    bool readSamples(int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer,
                     juce::int64 startSampleInFile, int numSamples) override;

    // Live playback position, as of the last decoded sample. Published through
    // atomics from readSamples(), so safe to call from any thread.
    int getCurrentRow()     const { return posRow.load(); }
    int getCurrentPattern() const { return posPattern.load(); }
    int getCurrentOrder()   const { return posOrder.load(); }

    // How far decoding has got into the current row, 0..1 (never reaches 1).
    // Measured from the sample where decoding crossed into the row, over the
    // length of the previous row (or speed/tempo before one has been measured).
    double getCurrentRowFraction() const;

    // Length of the current row in seconds (estimate), 0 if unknown.
    double getCurrentRowSeconds() const { return (double) rowLengthSamples.load() / kSampleRate; }

    // Per-channel output level (libopenmpt's channel VU, ~0..1) -- the
    // highest seen since the last call, which resets it, so a short hit
    // between two GUI polls isn't lost. Fills up to maxChannels entries of
    // dest; returns how many channels the module has (capped the same way).
    static constexpr int kMaxLevelChannels = 128;
    int takeChannelPeaks(float* dest, int maxChannels);

private:
    void publishPosition(bool afterSeek);

    std::unique_ptr<openmpt::module> mod;
    juce::int64 currentSample = 0;

    // Position published for the GUI / video export (see publishPosition()).
    std::atomic<int> posRow { -1 }, posPattern { -1 }, posOrder { -1 };
    std::atomic<juce::int64> decodedSample { 0 };     // == currentSample
    std::atomic<juce::int64> rowStartSample { -1 };   // -1: row start not seen (after a seek)
    std::atomic<juce::int64> rowLengthSamples { 0 };

    void publishChannelLevels();
    int numModuleChannels = 0;
    std::array<std::atomic<float>, kMaxLevelChannels> channelPeaks {};

    // Per-channel float decode buffers (non-interleaved)
    std::vector<float> decodeLeft, decodeRight;

    // 48 kHz gives libopenmpt the best internal resampling quality
    static constexpr int kSampleRate  = 48000;
    static constexpr int kNumChannels = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenMptAudioFormatReader)
};
#endif

/**
 * AudioFormat registrar for module tracker files.
 * Register with AudioFormatManager::registerFormat().
 */
class OpenMptAudioFormat : public juce::AudioFormat
{
public:
    OpenMptAudioFormat();

    juce::Array<int> getPossibleSampleRates() override;
    juce::Array<int> getPossibleBitDepths()   override;
    bool canDoStereo()  override { return true; }
    bool canDoMono()    override { return true; }
    bool isCompressed() override { return true; }

    juce::AudioFormatReader* createReaderFor(juce::InputStream* sourceStream,
                                             bool deleteStreamIfOpeningFails) override;
    juce::AudioFormatWriter* createWriterFor(juce::OutputStream*, double, unsigned int,
                                             int, const juce::StringPairArray&, int) override
    {
        return nullptr; // write not supported
    }
};

#endif // SOUNDPLAYER_OPENMPT_SUPPORT || SOUNDPLAYER_XMP_SUPPORT
