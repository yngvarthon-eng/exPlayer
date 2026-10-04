#include "AlbumArtPulsePlugin.h"
#include <cmath>

void AlbumArtPulsePlugin::update(const juce::AudioBuffer<float>& buffer,
                                 const juce::Array<float>& spectrum)
{
    // Bass envelope: peak magnitude across the lowest log-spaced band, smoothed
    // rise-fast/decay-slow so the pulse reads as a beat rather than flickering
    // with every FFT frame.
    const int numBins = spectrum.size();
    if (numBins > 1)
    {
        const int bandEnd = juce::jlimit(2, numBins,
            (int) std::pow((float) numBins, 1.0f / kNumBassBands));

        float peak = 0.0f;
        for (int b = 1; b < bandEnd; ++b)
            peak = juce::jmax(peak, spectrum[b]);

        if (peak > bassEnvelope)
            bassEnvelope += (peak - bassEnvelope) * kRiseRate;
        else
            bassEnvelope = juce::jmax(0.0f, bassEnvelope - kDecayRate);
    }

    // Waveform capture, mirroring WaveformPlugin's zero-crossing trigger
    // stabilization so the overlay doesn't jitter left-right frame to frame.
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    auto* data = buffer.getReadPointer(0);
    int triggerOffset = 0;
    const int searchEnd = numSamples / 4;
    for (int i = 1; i < searchEnd; ++i)
    {
        if (data[i - 1] < 0.0f && data[i] >= 0.0f)
        {
            triggerOffset = i;
            break;
        }
    }

    const int displaySamples = juce::jmin(numSamples - triggerOffset, numSamples * 3 / 4);
    waveformData.resize((size_t) displaySamples);
    for (int i = 0; i < displaySamples; ++i)
        waveformData[(size_t) i] = data[triggerOffset + i];
}

void AlbumArtPulsePlugin::render(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.fillAll(juce::Colours::black);

    if (! image.isValid())
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(16.0f));
        g.drawText("Click here to choose a picture to pulse to the beat",
                   bounds, juce::Justification::centred, true);
        return;
    }

    const auto frame = bounds.toFloat();

    // Scale from the frame's centre so the picture grows/shrinks in place on
    // each beat rather than sliding, and always covers the full frame (never
    // letterboxed) even while pulsing above 1x scale.
    const float scale = 1.0f + bassEnvelope * kPulseAmount;
    const float coverScale = juce::jmax(frame.getWidth() / (float) image.getWidth(),
                                        frame.getHeight() / (float) image.getHeight()) * scale;
    const float drawW = (float) image.getWidth() * coverScale;
    const float drawH = (float) image.getHeight() * coverScale;
    const float drawX = frame.getCentreX() - drawW * 0.5f;

    // verticalAnchor positions the picture's centre as a fraction of the frame's
    // height (0 = top edge, 0.5 = the old always-centred default, 1 = bottom
    // edge); clamped so the picture never reveals a black gap at the opposite
    // edge, however far it's dragged or however large the pulse scale grows.
    const float desiredCentreY = frame.getY() + verticalAnchor * frame.getHeight();
    const float drawY = juce::jlimit(frame.getBottom() - drawH, frame.getY(), desiredCentreY - drawH * 0.5f);

    g.drawImage(image, drawX, drawY, drawW, drawH, 0, 0, image.getWidth(), image.getHeight());

    // Translucent waveform overlay, so the picture stays visible underneath.
    // Follows the picture's own vertical position rather than the frame's.
    if (! waveformData.empty())
    {
        const float cy = drawY + drawH * 0.5f;
        const float halfH = frame.getHeight() * 0.22f;
        const int n = (int) waveformData.size();

        juce::Path path;
        path.startNewSubPath(frame.getX(), cy - waveformData[0] * halfH);
        for (int i = 1; i < n; ++i)
        {
            const float x = frame.getX() + (float) i / (float) (n - 1) * frame.getWidth();
            const float y = cy - waveformData[(size_t) i] * halfH;
            path.lineTo(x, y);
        }

        g.setColour(waveformColour.withAlpha(0.5f));
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }
}
