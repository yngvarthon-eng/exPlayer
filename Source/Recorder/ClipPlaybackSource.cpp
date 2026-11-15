#include "ClipPlaybackSource.h"

void ClipPlaybackSource::start(juce::int64 startSample, juce::int64 endSample)
{
    playing.store(false);
    ++startGeneration;
    position.store((double) startSample);
    endPosition.store((double) endSample);
    playing.store(endSample > startSample);
}

void ClipPlaybackSource::prepareToPlay(int, double sampleRate)
{
    outputSampleRate.store(sampleRate);
}

void ClipPlaybackSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    const auto generation = startGeneration.load();
    const double outRate = outputSampleRate.load();
    if (! playing.load() || outRate <= 0.0 || info.buffer->getNumChannels() == 0)
        return;

    const juce::ScopedTryLock sl(clip.getLock());
    if (! sl.isLocked())
        return; // an edit is moving samples right now; skip this block

    const auto clipLength = clip.getNumSamples();
    const int clipChannels = clip.getNumChannels();
    const double end = juce::jmin(endPosition.load(), (double) clipLength);
    if (clipChannels == 0 || clip.getSampleRate() <= 0.0)
    {
        playing.store(false);
        return;
    }

    const double step = clip.getSampleRate() / outRate;
    const auto& source = clip.getBuffer();
    const int outChannels = juce::jmin(2, info.buffer->getNumChannels());
    double pos = position.load();
    int written = 0;

    for (; written < info.numSamples && pos < end; ++written, pos += step)
    {
        const auto index = (juce::int64) pos;
        const auto next = juce::jmin(index + 1, clipLength - 1);
        const float frac = (float) (pos - (double) index);

        for (int ch = 0; ch < outChannels; ++ch)
        {
            const float* in = source.getReadPointer(juce::jmin(ch, clipChannels - 1));
            const float sample = in[index] + (in[next] - in[index]) * frac;
            info.buffer->setSample(ch, info.startSample + written, sample);
        }
    }

    if (startGeneration.load() != generation)
        return;

    position.store(pos);
    if (pos >= end)
        playing.store(false);
}
