#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Clean-up processing for recorded takes. Every function works in place on
// [start, start + length) of a buffer, so MicClip can run them as one undoable
// edit on a selection.
namespace ClipEffects
{
    // ---- Mono ---------------------------------------------------------------

    // Which channel a mono version should be taken from: the louder one when
    // the other is more than 10 dB quieter (a mono mic on a stereo jack puts
    // signal on one side and only noise on the other -- mixing them would cost
    // 6 dB of level and add that noise), otherwise -1 for "mix both".
    // Takes per-channel mean-square levels.
    int chooseMonoSource(float meanSquareLeft, float meanSquareRight);
    int chooseMonoSource(const juce::AudioBuffer<float>& audio, juce::int64 numSamples);

    // ---- Hum ----------------------------------------------------------------

    // 50 or 60, whichever mains frequency (with its 2nd harmonic) is stronger.
    double detectMainsHz(const juce::AudioBuffer<float>& audio, juce::int64 numSamples, double sampleRate);

    // Low cut at 70 Hz (4th-order Butterworth) plus narrow notches on the mains
    // frequency and its harmonics up to 500 Hz. Filters are warmed up on the
    // audio just before start, so the edit doesn't click at its left edge.
    void removeHum(juce::AudioBuffer<float>& audio, int start, int length, double sampleRate, double mainsHz);

    // ---- Noise reduction ------------------------------------------------------

    // RMS magnitude spectrum of the noise, per channel.
    struct NoiseProfile
    {
        std::vector<std::vector<float>> magnitudes; // [channel][bin]
        int frames = 0;                             // how many frames it was averaged over
        bool isValid() const { return ! magnitudes.empty(); }
    };

    constexpr int fftOrder = 11;              // 2048-point frames
    constexpr int fftSize  = 1 << fftOrder;

    // Learns the profile from a stretch that contains only noise. Fails (returns
    // an invalid profile) if it is shorter than one frame.
    NoiseProfile learnNoiseProfile(const juce::AudioBuffer<float>& audio, int start, int length);

    // Learns it from the quietest 10% of the range instead -- the pauses
    // between words, typically -- for when no noise-only selection was made.
    NoiseProfile estimateNoiseProfile(const juce::AudioBuffer<float>& audio, int start, int length);

    // Start of the quietest stretch of `length` samples in [0, numSamples)
    // (ignoring digital silence), or -1 if the audio is shorter than that.
    // Used to pick a noise-only stretch when the user hasn't selected one.
    juce::int64 findQuietestStretch(const juce::AudioBuffer<float>& audio, juce::int64 numSamples, int length);

    // Turns down background hiss with a Wiener filter whose per-bin SNR is
    // smoothed over time (decision-directed, Ephraim & Malah): bins well above
    // the noise -- voice harmonics, sibilants -- pass nearly untouched, noisy
    // ones are lowered smoothly to at most reductionDb. overestimateDb treats
    // the noise as that much louder than profiled (more removal, more risk to
    // quiet detail).
    void reduceNoise(juce::AudioBuffer<float>& audio, int start, int length,
                     const NoiseProfile& profile, float reductionDb, float overestimateDb = 0.0f);
}
