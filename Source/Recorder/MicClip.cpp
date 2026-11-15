#include "MicClip.h"

double MicClip::getLengthSeconds() const
{
    return sampleRate > 0.0 ? (double) numSamples / sampleRate : 0.0;
}

MicClip::SampleRange MicClip::clampRange(SampleRange range) const
{
    return range.getIntersectionWith({ 0, numSamples });
}

void MicClip::clear()
{
    {
        const juce::ScopedLock sl(lock);
        buffer.setSize(0, 0);
        numSamples = 0;
        sampleRate = 0.0;
        peaks.clear();
    }
    undoStack.clear();
    redoStack.clear();
    changed();
}

void MicClip::ensureCapacity(juce::int64 samplesNeeded)
{
    if (samplesNeeded <= buffer.getNumSamples())
        return;

    // Grow geometrically so a long recording costs O(n) copying overall, not
    // one reallocation per 30 Hz drain.
    const auto newCapacity = juce::jmax(samplesNeeded, (juce::int64) buffer.getNumSamples() * 2,
                                        (juce::int64) 1 << 16);
    buffer.setSize(buffer.getNumChannels(), (int) newCapacity, true, true, false);
}

bool MicClip::append(const juce::AudioBuffer<float>& source, int numSourceSamples, double rate)
{
    if (numSourceSamples <= 0 || source.getNumChannels() == 0)
        return false;

    if (numSamples > 0 && std::abs(rate - sampleRate) > 0.5)
        return false;

    const auto firstNewSample = numSamples;
    {
        const juce::ScopedLock sl(lock);

        if (numSamples == 0)
        {
            sampleRate = rate;
            const int channels = juce::jmin(2, source.getNumChannels());
            buffer.setSize(channels, 0);
            peaks.assign((size_t) channels, {});
        }

        ensureCapacity(numSamples + numSourceSamples);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const int sourceChannel = ch < source.getNumChannels() ? ch : 0;
            buffer.copyFrom(ch, (int) numSamples, source, sourceChannel, 0, numSourceSamples);
        }

        numSamples += numSourceSamples;
    }

    updatePeaksFrom(firstNewSample);
    changed();
    return true;
}

// ---- Peaks ----

void MicClip::rebuildPeaks()
{
    peaks.assign((size_t) buffer.getNumChannels(), {});
    updatePeaksFrom(0);
}

void MicClip::updatePeaksFrom(juce::int64 firstSample)
{
    const auto firstBin = firstSample / peakBinSize;
    const auto numBins = (numSamples + peakBinSize - 1) / peakBinSize;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto& channelPeaks = peaks[(size_t) ch];
        channelPeaks.resize((size_t) numBins);
        const float* data = buffer.getReadPointer(ch);

        for (auto bin = firstBin; bin < numBins; ++bin)
        {
            const auto start = bin * peakBinSize;
            const auto end = juce::jmin(numSamples, start + peakBinSize);
            auto range = juce::FloatVectorOperations::findMinAndMax(data + start, (int) (end - start));
            channelPeaks[(size_t) bin] = { range.getStart(), range.getEnd() };
        }
    }
}

// ---- Undo ----

MicClip::Snapshot MicClip::makeSnapshot() const
{
    Snapshot snapshot;
    snapshot.sampleRate = sampleRate;
    snapshot.audio.setSize(buffer.getNumChannels(), (int) numSamples);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        snapshot.audio.copyFrom(ch, 0, buffer, ch, 0, (int) numSamples);
    return snapshot;
}

void MicClip::restoreSnapshot(Snapshot&& snapshot)
{
    {
        const juce::ScopedLock sl(lock);
        numSamples = snapshot.audio.getNumSamples();
        sampleRate = snapshot.sampleRate;
        buffer = std::move(snapshot.audio);
        rebuildPeaks();
    }
    changed();
}

void MicClip::trimUndoHistory()
{
    auto bytesOf = [](const Snapshot& s)
    {
        return (size_t) s.audio.getNumChannels() * (size_t) s.audio.getNumSamples() * sizeof(float);
    };

    size_t total = 0;
    for (const auto& s : undoStack) total += bytesOf(s);
    for (const auto& s : redoStack) total += bytesOf(s);

    // Drop the oldest undo steps first, but always keep the most recent one.
    while (total > maxUndoBytes && undoStack.size() > 1)
    {
        total -= bytesOf(undoStack.front());
        undoStack.erase(undoStack.begin());
    }
}

void MicClip::pushUndoState()
{
    undoStack.push_back(makeSnapshot());
    redoStack.clear();
    trimUndoHistory();
}

bool MicClip::undo()
{
    if (undoStack.empty())
        return false;

    redoStack.push_back(makeSnapshot());
    auto snapshot = std::move(undoStack.back());
    undoStack.pop_back();
    restoreSnapshot(std::move(snapshot));
    trimUndoHistory();
    return true;
}

bool MicClip::redo()
{
    if (redoStack.empty())
        return false;

    undoStack.push_back(makeSnapshot());
    auto snapshot = std::move(redoStack.back());
    redoStack.pop_back();
    restoreSnapshot(std::move(snapshot));
    trimUndoHistory();
    return true;
}

// ---- Edits ----

bool MicClip::deleteRange(SampleRange range)
{
    range = clampRange(range);
    if (range.isEmpty())
        return false;

    pushUndoState();
    {
        const juce::ScopedLock sl(lock);
        const auto tail = numSamples - range.getEnd();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            std::memmove(data + range.getStart(), data + range.getEnd(), (size_t) tail * sizeof(float));
        }
        numSamples -= range.getLength();
        rebuildPeaks();
    }
    changed();
    return true;
}

bool MicClip::keepOnly(SampleRange range)
{
    range = clampRange(range);
    if (range.isEmpty() || range == SampleRange(0, numSamples))
        return false;

    pushUndoState();
    {
        const juce::ScopedLock sl(lock);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            std::memmove(data, data + range.getStart(), (size_t) range.getLength() * sizeof(float));
        }
        numSamples = range.getLength();
        rebuildPeaks();
    }
    changed();
    return true;
}

bool MicClip::silence(SampleRange range)
{
    range = clampRange(range);
    if (range.isEmpty())
        return false;

    pushUndoState();
    {
        const juce::ScopedLock sl(lock);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, (int) range.getStart(), (int) range.getLength());
        updatePeaksFrom(range.getStart());
    }
    changed();
    return true;
}

bool MicClip::fadeIn(SampleRange range)
{
    range = clampRange(range);
    if (range.getLength() < 2)
        return false;

    pushUndoState();
    {
        const juce::ScopedLock sl(lock);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGainRamp(ch, (int) range.getStart(), (int) range.getLength(), 0.0f, 1.0f);
        updatePeaksFrom(range.getStart());
    }
    changed();
    return true;
}

bool MicClip::fadeOut(SampleRange range)
{
    range = clampRange(range);
    if (range.getLength() < 2)
        return false;

    pushUndoState();
    {
        const juce::ScopedLock sl(lock);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGainRamp(ch, (int) range.getStart(), (int) range.getLength(), 1.0f, 0.0f);
        updatePeaksFrom(range.getStart());
    }
    changed();
    return true;
}

bool MicClip::normalize(SampleRange range, float targetPeak)
{
    range = range.isEmpty() ? SampleRange(0, numSamples) : clampRange(range);
    if (range.isEmpty())
        return false;

    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax(peak, buffer.getMagnitude(ch, (int) range.getStart(), (int) range.getLength()));

    if (peak < 1.0e-6f)
        return false;

    pushUndoState();
    {
        const juce::ScopedLock sl(lock);
        buffer.applyGain((int) range.getStart(), (int) range.getLength(), targetPeak / peak);
        updatePeaksFrom(range.getStart());
    }
    changed();
    return true;
}

bool MicClip::applyEffect(SampleRange range, const Effect& process)
{
    range = range.isEmpty() ? SampleRange(0, numSamples) : clampRange(range);
    if (range.isEmpty())
        return false;

    auto before = makeSnapshot();
    bool changedSomething = false;
    {
        const juce::ScopedLock sl(lock);
        changedSomething = process(buffer, (int) range.getStart(), (int) range.getLength());
        if (changedSomething)
            updatePeaksFrom(range.getStart());
    }

    if (! changedSomething)
        return false;

    undoStack.push_back(std::move(before));
    redoStack.clear();
    trimUndoHistory();
    changed();
    return true;
}

bool MicClip::makeMono(int sourceChannel)
{
    if (getNumChannels() < 2 || isEmpty())
        return false;

    pushUndoState();
    {
        const juce::ScopedLock sl(lock);
        juce::AudioBuffer<float> mono(1, buffer.getNumSamples());
        if (sourceChannel == 0 || sourceChannel == 1)
        {
            mono.copyFrom(0, 0, buffer, sourceChannel, 0, (int) numSamples);
        }
        else
        {
            mono.copyFrom(0, 0, buffer.getReadPointer(0), (int) numSamples, 0.5f);
            mono.addFrom(0, 0, buffer, 1, 0, (int) numSamples, 0.5f);
        }
        buffer = std::move(mono);
        rebuildPeaks();
    }
    changed();
    return true;
}

// ---- Saving ----

bool MicClip::writeToFile(const juce::File& file, juce::String& errorMessage) const
{
    if (isEmpty())
    {
        errorMessage = "There is nothing recorded to save.";
        return false;
    }

    std::unique_ptr<juce::AudioFormat> format;
    if (file.hasFileExtension(".flac"))
        format = std::make_unique<juce::FlacAudioFormat>();
    else
        format = std::make_unique<juce::WavAudioFormat>();

    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    if (stream->failedToOpen())
    {
        errorMessage = "Could not open " + file.getFullPathName() + " for writing.";
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        format->createWriterFor(stream.get(), sampleRate, (unsigned int) getNumChannels(), 24, {}, 0));
    if (writer == nullptr)
    {
        errorMessage = "The " + format->getFormatName() + " writer rejected "
                     + juce::String(sampleRate, 0) + " Hz / " + juce::String(getNumChannels()) + " ch.";
        return false;
    }
    stream.release(); // the writer owns it now

    if (! writer->writeFromAudioSampleBuffer(buffer, 0, (int) numSamples))
    {
        errorMessage = "Writing " + file.getFileName() + " failed (disk full?).";
        return false;
    }

    return true;
}
