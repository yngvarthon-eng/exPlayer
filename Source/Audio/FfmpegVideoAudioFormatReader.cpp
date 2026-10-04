#include "FfmpegVideoAudioFormatReader.h"
#include "../Export/VideoExporter.h"
#include "../Export/FfmpegProcess.h"
#include <cstring>
#include <cstdlib>

namespace
{
    constexpr int kProbeBytes = 4096;

    juce::StringArray buildArgs(const juce::File& file, double seekSeconds, int audioTrackIndex)
    {
        juce::StringArray args { "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin" };

        if (seekSeconds > 0.0)
        {
            args.add("-ss");
            args.add(juce::String(seekSeconds, 3));
            // Explicit rather than relying on ffmpeg's own default: forces
            // decode-forward-to-exact-timestamp after the nearest preceding
            // keyframe instead of snapping to whatever keyframe -ss's normal
            // input-seek lands on.
            args.add("-accurate_seek");
        }

        args.add("-i");
        args.add(file.getFullPathName());

        if (audioTrackIndex >= 0)
        {
            // An explicit -map already selects exactly one stream, so mixing in
            // the legacy -vn/-sn/-dn exclusion flags alongside it is redundant
            // and (per ffmpeg's own docs) not guaranteed to combine cleanly.
            args.add("-map");
            args.add("0:a:" + juce::String(audioTrackIndex));
        }
        else
        {
            args.add("-vn"); args.add("-sn"); args.add("-dn");
        }

        args.add("-f"); args.add("s16le");
        args.add("-acodec"); args.add("pcm_s16le");
        args.add("-ar"); args.add(juce::String((int) FfmpegVideoAudioFormatReader::kSampleRate));
        args.add("-ac"); args.add(juce::String(FfmpegVideoAudioFormatReader::kChannels));
        args.add("-");
        return args;
    }
}

std::unique_ptr<FfmpegVideoAudioFormatReader> FfmpegVideoAudioFormatReader::createForFile(
    const juce::File& file, double durationSeconds, int audioTrackIndex, juce::String& errorMessage)
{
    if (!VideoExporter::isFfmpegAvailable())
    {
        errorMessage = "ffmpeg was not found on PATH.";
        return nullptr;
    }

    auto process = std::make_unique<juce::ChildProcess>();
    if (!process->start(buildArgs(file, 0.0, audioTrackIndex), juce::ChildProcess::wantStdOut))
    {
        errorMessage = "Could not start ffmpeg to decode '" + file.getFileName() + "'.";
        return nullptr;
    }

    // Validate the file actually produces audio before handing back a reader --
    // same rationale as FfmpegAudioFormatReader::createForUrl().
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
        errorMessage = "ffmpeg could not decode audio from '" + file.getFileName() + "'.";
        FfmpegProcess::killAndReap(*process, "FfmpegVideoAudioFormatReader");
        return nullptr;
    }

    std::unique_ptr<FfmpegVideoAudioFormatReader> reader(
        new FfmpegVideoAudioFormatReader(file, std::move(process), durationSeconds, audioTrackIndex));
    reader->pendingBytes.append(probe, (size_t) probeBytes);
    return reader;
}

FfmpegVideoAudioFormatReader::FfmpegVideoAudioFormatReader(
    juce::File f, std::unique_ptr<juce::ChildProcess> p, double durationSeconds, int trackIndex)
    : juce::AudioFormatReader(nullptr, "FFmpeg"), file(std::move(f)), audioTrackIndex(trackIndex), process(std::move(p))
{
    sampleRate = kSampleRate;
    bitsPerSample = 16;
    numChannels = (unsigned int) kChannels;
    usesFloatingPointData = false;
    lengthInSamples = (juce::int64) (durationSeconds * kSampleRate);
}

FfmpegVideoAudioFormatReader::~FfmpegVideoAudioFormatReader()
{
    if (process != nullptr)
        FfmpegProcess::killAndReap(*process, "FfmpegVideoAudioFormatReader");
}

bool FfmpegVideoAudioFormatReader::relaunchAt(juce::int64 targetSample)
{
    if (process != nullptr)
        FfmpegProcess::killAndReap(*process, "FfmpegVideoAudioFormatReader");

    pendingBytes.reset();
    endOfStream = false;

    const double seekSeconds = (double) targetSample / kSampleRate;
    auto newProcess = std::make_unique<juce::ChildProcess>();
    if (!newProcess->start(buildArgs(file, seekSeconds, audioTrackIndex), juce::ChildProcess::wantStdOut))
    {
        process.reset();
        expectedNextSample = targetSample;
        return false;
    }

    process = std::move(newProcess);
    expectedNextSample = targetSample;
    return true;
}

bool FfmpegVideoAudioFormatReader::readSamples(int* const* destChannels, int numDestChannels,
                                               int startOffsetInDestBuffer, juce::int64 startSampleInFile,
                                               int numSamples)
{
    if (std::abs((long long) (startSampleInFile - expectedNextSample)) > kSeekToleranceSamples)
        relaunchAt(startSampleInFile);

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

    while (filled < totalBytes && !endOfStream && process != nullptr)
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

    expectedNextSample = startSampleInFile + numSamples;
    return true;
}
