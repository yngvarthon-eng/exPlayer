#include "ClipEffects.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace ClipEffects
{

// ==============================================================================
// Mono
// ==============================================================================

int chooseMonoSource(float meanSquareLeft, float meanSquareRight)
{
    constexpr float tenDb = 10.0f; // power ratio for 10 dB
    if (meanSquareRight > meanSquareLeft * tenDb)
        return 1;
    if (meanSquareLeft > meanSquareRight * tenDb)
        return 0;
    return -1;
}

int chooseMonoSource(const juce::AudioBuffer<float>& audio, juce::int64 numSamples)
{
    if (audio.getNumChannels() < 2 || numSamples <= 0)
        return 0;

    auto meanSquare = [&](int ch)
    {
        const float rms = audio.getRMSLevel(ch, 0, (int) numSamples);
        return rms * rms;
    };
    return chooseMonoSource(meanSquare(0), meanSquare(1));
}

// ==============================================================================
// Hum
// ==============================================================================

namespace
{
    // Power of one frequency over a channel mix (Goertzel).
    double toneEnergy(const juce::AudioBuffer<float>& audio, int numSamples, double sampleRate, double hz)
    {
        const double coeff = 2.0 * std::cos(juce::MathConstants<double>::twoPi * hz / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            double x = 0.0;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                x += audio.getSample(ch, i);
            const double s = x + coeff * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }
}

double detectMainsHz(const juce::AudioBuffer<float>& audio, juce::int64 numSamples, double sampleRate)
{
    // Ten seconds is plenty to tell 50 from 60 Hz.
    const int n = (int) juce::jmin(numSamples, (juce::int64) (sampleRate * 10.0));
    if (n <= 0 || sampleRate <= 0.0)
        return 50.0;

    const double at50 = toneEnergy(audio, n, sampleRate, 50.0) + toneEnergy(audio, n, sampleRate, 100.0);
    const double at60 = toneEnergy(audio, n, sampleRate, 60.0) + toneEnergy(audio, n, sampleRate, 120.0);
    return at60 > at50 ? 60.0 : 50.0;
}

void removeHum(juce::AudioBuffer<float>& audio, int start, int length, double sampleRate, double mainsHz)
{
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    if (length <= 0 || sampleRate <= 0.0)
        return;

    // 4th-order Butterworth high-pass as two biquads (Q values of that alignment).
    std::vector<Coefficients::Ptr> stages {
        Coefficients::makeHighPass(sampleRate, 70.0f, 0.5412f),
        Coefficients::makeHighPass(sampleRate, 70.0f, 1.3066f)
    };

    // ~4 Hz wide notches: deep on the hum, too narrow to dull a voice.
    for (double hz = mainsHz; hz <= 500.0 && hz < sampleRate * 0.45; hz += mainsHz)
        stages.push_back(Coefficients::makeNotch(sampleRate, (float) hz, (float) (hz / 4.0)));

    const int warmUp = juce::jmin(start, (int) (sampleRate * 0.5));

    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        std::vector<juce::dsp::IIR::Filter<float>> filters;
        for (auto& c : stages)
            filters.emplace_back(c);

        float* data = audio.getWritePointer(ch);
        auto run = [&filters](float x)
        {
            for (auto& f : filters)
                x = f.processSample(x);
            return x;
        };

        for (int i = start - warmUp; i < start; ++i)
            run(data[i]);
        for (int i = start; i < start + length; ++i)
            data[i] = run(data[i]);
    }
}

// ==============================================================================
// Noise reduction
// ==============================================================================

namespace
{
    constexpr int numBins = fftSize / 2 + 1;

    const std::vector<float>& hannWindow()
    {
        static const std::vector<float> window = []
        {
            std::vector<float> w(fftSize);
            for (int i = 0; i < fftSize; ++i)
                w[(size_t) i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
            return w;
        }();
        return window;
    }

    // Windowed magnitude spectrum of fftSize samples starting at data.
    void magnitudeSpectrum(juce::dsp::FFT& fft, const float* data, std::vector<float>& work, float* magsOut)
    {
        const auto& window = hannWindow();
        for (int i = 0; i < fftSize; ++i)
            work[(size_t) i] = data[i] * window[(size_t) i];
        std::fill(work.begin() + fftSize, work.end(), 0.0f);
        fft.performRealOnlyForwardTransform(work.data(), true);
        for (int b = 0; b < numBins; ++b)
            magsOut[b] = std::hypot(work[(size_t) (2 * b)], work[(size_t) (2 * b + 1)]);
    }

    NoiseProfile averageFrames(const juce::AudioBuffer<float>& audio, const std::vector<int>& frameStarts)
    {
        NoiseProfile profile;
        if (frameStarts.empty())
            return profile;

        juce::dsp::FFT fft(fftOrder);
        std::vector<float> work(2 * fftSize), mags(numBins);

        profile.magnitudes.assign((size_t) audio.getNumChannels(), std::vector<float>(numBins, 0.0f));
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            auto& sum = profile.magnitudes[(size_t) ch];
            for (int frameStart : frameStarts)
            {
                magnitudeSpectrum(fft, audio.getReadPointer(ch, frameStart), work, mags.data());
                for (int b = 0; b < numBins; ++b)
                    sum[(size_t) b] += mags[(size_t) b] * mags[(size_t) b];
            }
            // Stored as RMS magnitude: the Wiener gain works from noise power.
            for (auto& m : sum)
                m = std::sqrt(m / (float) frameStarts.size());
        }
        profile.frames = (int) frameStarts.size();
        return profile;
    }
}

NoiseProfile learnNoiseProfile(const juce::AudioBuffer<float>& audio, int start, int length)
{
    std::vector<int> frames;
    for (int pos = start; pos + fftSize <= start + length; pos += fftSize / 2)
        frames.push_back(pos);
    return averageFrames(audio, frames);
}

NoiseProfile estimateNoiseProfile(const juce::AudioBuffer<float>& audio, int start, int length)
{
    std::vector<std::pair<double, int>> energies; // (energy, frame start)
    for (int pos = start; pos + fftSize <= start + length; pos += fftSize / 2)
    {
        double energy = 0.0;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const float rms = audio.getRMSLevel(ch, pos, fftSize);
            energy += (double) rms * rms;
        }
        // Digital silence (an edit's "Silence", a muted start) isn't the noise
        // we're after and would make every quiet bin look like signal.
        if (energy > 1.0e-14)
            energies.emplace_back(energy, pos);
    }

    if (energies.empty())
        return {};

    std::sort(energies.begin(), energies.end());
    const size_t count = juce::jmin(energies.size(), juce::jmax((size_t) 4, energies.size() / 10));

    std::vector<int> frames;
    for (size_t i = 0; i < count; ++i)
        frames.push_back(energies[i].second);
    return averageFrames(audio, frames);
}

juce::int64 findQuietestStretch(const juce::AudioBuffer<float>& audio, juce::int64 numSamples, int length)
{
    if (length <= 0 || numSamples < length)
        return -1;

    const int step = juce::jmax(1, length / 8);
    juce::int64 best = -1;
    double bestEnergy = 0.0;

    for (juce::int64 pos = 0; pos + length <= numSamples; pos += step)
    {
        double energy = 0.0;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const float rms = audio.getRMSLevel(ch, (int) pos, length);
            energy += (double) rms * rms;
        }
        if (energy > 1.0e-14 && (best < 0 || energy < bestEnergy))
        {
            best = pos;
            bestEnergy = energy;
        }
    }
    return best;
}

void reduceNoise(juce::AudioBuffer<float>& audio, int start, int length,
                 const NoiseProfile& profile, float reductionDb, float overestimateDb)
{
    if (! profile.isValid() || length <= 0)
        return;

    constexpr int hop = fftSize / 4;   // 75% overlap
    // Decision-directed smoothing of the a-priori SNR (Ephraim & Malah). This is
    // what keeps the result free of the warbling "musical noise" a per-bin gate
    // produces: a bin's gain follows its SNR smoothly from frame to frame.
    constexpr float alpha = 0.98f;

    const float floorGain = juce::Decibels::decibelsToGain(-reductionDb);
    const float overestimate = juce::Decibels::decibelsToGain(overestimateDb * 2.0f); // as power
    const auto& window = hannWindow();

    juce::dsp::FFT fft(fftOrder);
    std::vector<float> work(2 * fftSize);
    std::vector<float> gains(numBins), previousCleanSnr(numBins);

    // The range is padded with a frame of zeros either side so its edges get
    // the same full overlap as the middle.
    const size_t paddedLength = (size_t) length + 2 * fftSize;
    std::vector<float> input(paddedLength), output(paddedLength), windowSum(paddedLength);

    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        const auto& noise = profile.magnitudes[(size_t) juce::jmin(ch, (int) profile.magnitudes.size() - 1)];

        std::fill(input.begin(), input.end(), 0.0f);
        std::fill(output.begin(), output.end(), 0.0f);
        std::fill(windowSum.begin(), windowSum.end(), 0.0f);
        std::fill(previousCleanSnr.begin(), previousCleanSnr.end(), 0.0f);
        std::copy(audio.getReadPointer(ch, start), audio.getReadPointer(ch, start) + length, input.begin() + fftSize);

        for (size_t pos = 0; pos + fftSize <= paddedLength; pos += hop)
        {
            for (int i = 0; i < fftSize; ++i)
                work[(size_t) i] = input[pos + (size_t) i] * window[(size_t) i];
            std::fill(work.begin() + fftSize, work.end(), 0.0f);
            fft.performRealOnlyForwardTransform(work.data());

            for (int b = 0; b < numBins; ++b)
            {
                const float re = work[(size_t) (2 * b)], im = work[(size_t) (2 * b + 1)];
                const float noisePower = juce::jmax(1.0e-20f, noise[(size_t) b] * noise[(size_t) b] * overestimate);
                const float postSnr = (re * re + im * im) / noisePower;

                // A-priori SNR: mostly last frame's cleaned estimate, nudged by this
                // frame's evidence. Wiener gain from it, never below the floor.
                const float prioriSnr = alpha * previousCleanSnr[(size_t) b]
                                      + (1.0f - alpha) * juce::jmax(0.0f, postSnr - 1.0f);
                const float gain = juce::jmax(floorGain, prioriSnr / (1.0f + prioriSnr));

                gains[(size_t) b] = gain;
                previousCleanSnr[(size_t) b] = gain * gain * postSnr;
            }

            // Apply to each bin and its negative-frequency mirror.
            for (int b = 0; b < numBins; ++b)
            {
                work[(size_t) (2 * b)]     *= gains[(size_t) b];
                work[(size_t) (2 * b + 1)] *= gains[(size_t) b];
                if (b > 0 && b < fftSize / 2)
                {
                    const int mirror = fftSize - b;
                    work[(size_t) (2 * mirror)]     *= gains[(size_t) b];
                    work[(size_t) (2 * mirror + 1)] *= gains[(size_t) b];
                }
            }

            fft.performRealOnlyInverseTransform(work.data());
            for (int i = 0; i < fftSize; ++i)
            {
                output[pos + (size_t) i] += work[(size_t) i] * window[(size_t) i];
                windowSum[pos + (size_t) i] += window[(size_t) i] * window[(size_t) i];
            }
        }

        float* data = audio.getWritePointer(ch, start);
        for (int i = 0; i < length; ++i)
        {
            const float norm = windowSum[(size_t) (fftSize + i)];
            if (norm > 1.0e-6f)
                data[i] = output[(size_t) (fftSize + i)] / norm;
        }
    }
}

} // namespace ClipEffects
