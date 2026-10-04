#include "FfmpegAudioFormatReader.h"
#include "../Export/VideoExporter.h"
#include "../Export/FfmpegProcess.h"
#include <cstring>
#include <limits>

namespace
{
    constexpr int kProbeBytes = 4096;
}

std::unique_ptr<FfmpegAudioFormatReader> FfmpegAudioFormatReader::createForUrl(
    const juce::String& url, juce::String& errorMessage)
{
    if (!VideoExporter::isFfmpegAvailable())
    {
        errorMessage = "ffmpeg was not found on PATH.";
        return nullptr;
    }

    // Passed as argv elements (not a shell string), so the URL needs no quoting.
    juce::StringArray args {
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
        "-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "4",
        "-rw_timeout", "10000000",
        "-i", url,
        "-vn", "-sn", "-dn",
        "-f", "s16le", "-acodec", "pcm_s16le",
        "-ar", juce::String((int) kSampleRate), "-ac", juce::String(kChannels), "-"
    };

    auto process = std::make_unique<juce::ChildProcess>();
    if (!process->start(args, juce::ChildProcess::wantStdOut))
    {
        errorMessage = "Could not start ffmpeg for stream decoding.";
        return nullptr;
    }

    // ffmpeg exits almost immediately on a bad URL or a stream it can't decode.
    // readProcessOutput() blocks until data arrives or the pipe closes (bounded by
    // -rw_timeout above), so a short blocking read here distinguishes success from
    // failure without a separate timeout/polling mechanism.
    char probe[kProbeBytes];
    int probeBytes = 0;
    while (probeBytes < kProbeBytes)
    {
        const int n = process->readProcessOutput(probe + probeBytes, kProbeBytes - probeBytes);
        if (n <= 0)
            break;
        probeBytes += n;
    }

    if (probeBytes <= 0)
    {
        errorMessage = "ffmpeg could not decode this stream.";
        process->kill();
        return nullptr;
    }

    std::unique_ptr<FfmpegAudioFormatReader> reader(new FfmpegAudioFormatReader(std::move(process)));
    reader->pendingBytes.append(probe, (size_t) probeBytes);
    return reader;
}

FfmpegAudioFormatReader::FfmpegAudioFormatReader(std::unique_ptr<juce::ChildProcess> ffmpegProcess)
    : juce::AudioFormatReader(nullptr, "FFmpeg"), process(std::move(ffmpegProcess))
{
    sampleRate = kSampleRate;
    bitsPerSample = 16;
    numChannels = (unsigned int) kChannels;
    usesFloatingPointData = false;

    // Duration is unknown for a live stream. MultiChannelAudioFormatReaderSource
    // computes how much is left to read as (lengthInSamples - position), so this
    // just needs to be far larger than any single playback session will reach.
    lengthInSamples = std::numeric_limits<juce::int64>::max() / 2;
}

FfmpegAudioFormatReader::~FfmpegAudioFormatReader()
{
    if (process != nullptr)
        FfmpegProcess::killAndReap(*process, "FfmpegAudioFormatReader");
}

bool FfmpegAudioFormatReader::readSamples(int* const* destChannels, int numDestChannels,
                                          int startOffsetInDestBuffer, juce::int64 /*startSampleInFile*/,
                                          int numSamples)
{
    constexpr int frameBytes = kChannels * 2; // stereo, 16-bit
    const int totalBytes = numSamples * frameBytes;

    juce::HeapBlock<char> frameData((size_t) totalBytes);
    int filled = 0;

    if (pendingBytes.getSize() > 0)
    {
        const int fromPending = (int) juce::jmin((size_t) totalBytes, pendingBytes.getSize());
        std::memcpy(frameData.getData(), pendingBytes.getData(), (size_t) fromPending);
        pendingBytes.removeSection(0, (size_t) fromPending);
        filled = fromPending;
    }

    while (filled < totalBytes && !endOfStream)
    {
        const int n = process->readProcessOutput(frameData.getData() + filled, totalBytes - filled);
        if (n <= 0)
        {
            endOfStream = true;
            break;
        }
        filled += n;
    }

    const int samplesFilled = filled / frameBytes;
    const auto* src = reinterpret_cast<const int16_t*>(frameData.getData());

    for (int ch = 0; ch < numDestChannels; ++ch)
    {
        if (auto* d = destChannels[ch])
        {
            const int srcCh = juce::jmin(ch, kChannels - 1);
            for (int i = 0; i < samplesFilled; ++i)
                d[startOffsetInDestBuffer + i] = static_cast<int>(src[i * kChannels + srcCh]) * 65536;
            for (int i = samplesFilled; i < numSamples; ++i)
                d[startOffsetInDestBuffer + i] = 0;
        }
    }

    return true;
}
