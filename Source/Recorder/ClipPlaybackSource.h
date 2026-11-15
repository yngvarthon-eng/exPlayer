#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "MicClip.h"

// Plays a range of a MicClip, resampled (linear interpolation) from the clip's
// own rate to whatever rate it is prepared at. Installed on AudioEngine as its
// auxiliary source, so a take is previewed through the same output device as
// everything else rather than opening a second one.
//
// start()/stop() are called from the message thread; getNextAudioBlock() runs
// on the audio thread and only ever try-locks the clip (see MicClip).
class ClipPlaybackSource : public juce::AudioSource
{
public:
    explicit ClipPlaybackSource(const MicClip& clipToPlay) : clip(clipToPlay) {}

    // Plays [startSample, endSample) of the clip, in clip samples.
    void start(juce::int64 startSample, juce::int64 endSample);
    void stop() { playing.store(false); }

    bool isPlaying() const { return playing.load(); }
    // Current position in clip samples (meaningful while playing).
    double getPositionSamples() const { return position.load(); }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

private:
    const MicClip& clip;
    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 };
    std::atomic<double> endPosition { 0.0 };
    std::atomic<double> outputSampleRate { 0.0 };
    // Bumped by start() so a block that was mid-render when the user hit Play
    // again doesn't write its stale position back over the new one.
    std::atomic<juce::uint32> startGeneration { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipPlaybackSource)
};
