#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <vector>

// One microphone take, held in memory, plus the edits the mic recorder window
// offers on it (trim, delete, silence, fades, normalize) and their undo/redo.
//
// Everything that changes the clip runs on the message thread. The only other
// reader is ClipPlaybackSource on the audio thread, which takes getLock() with
// a try-lock -- so every mutation here holds that lock for as long as samples
// are being moved, and the playback side simply outputs silence for the odd
// block where it loses the race.
//
// Sample ranges are in clip samples and are clamped to the clip, so callers
// can pass a selection straight from the waveform view.
class MicClip
{
public:
    using SampleRange = juce::Range<juce::int64>;

    // Size of one bin in the min/max overview used to draw long clips without
    // touching every sample on each repaint.
    static constexpr int peakBinSize = 256;

    struct PeakBin { float min = 0.0f, max = 0.0f; };

    MicClip() = default;

    double      getSampleRate() const  { return sampleRate; }
    int         getNumChannels() const { return buffer.getNumChannels(); }
    juce::int64 getNumSamples() const  { return numSamples; }
    double      getLengthSeconds() const;
    bool        isEmpty() const        { return numSamples == 0; }

    // The samples, valid for [0, getNumSamples()). The buffer itself may be
    // longer (spare capacity for recording), so never use its getNumSamples().
    const juce::AudioBuffer<float>& getBuffer() const { return buffer; }
    juce::CriticalSection& getLock() const            { return lock; }

    // Peaks for one channel, one bin per peakBinSize samples (the last bin may
    // be partial).
    const std::vector<PeakBin>& getPeaks(int channel) const { return peaks[(size_t) channel]; }

    // Bumped on every change, so views can tell when to repaint.
    juce::uint32 getRevision() const { return revision; }

    // Drops all audio and the undo history.
    void clear();

    // Appends numSamples from source. The first append into an empty clip fixes
    // the clip's sample rate and channel count; later appends map channels
    // (extra source channels are dropped, missing ones copy channel 0).
    // Returns false (and appends nothing) if rate differs from the clip's.
    bool append(const juce::AudioBuffer<float>& source, int numSourceSamples, double rate);

    // ---- Edits (each pushes an undo step, and returns false if it did nothing) --

    bool deleteRange(SampleRange range);
    bool keepOnly(SampleRange range);      // "trim": delete everything outside range
    bool silence(SampleRange range);
    bool fadeIn(SampleRange range);
    bool fadeOut(SampleRange range);
    // Scales range (or the whole clip if range is empty) so its loudest sample
    // reaches targetPeak. No-op on pure silence.
    bool normalize(SampleRange range, float targetPeak = 0.891f); // -1 dBFS

    // Runs process over range (or the whole clip if range is empty) as one
    // undoable edit; process gets the buffer, a start and a length and returns
    // false if it changed nothing. Used for the ClipEffects clean-up tools.
    using Effect = std::function<bool(juce::AudioBuffer<float>&, int start, int length)>;
    bool applyEffect(SampleRange range, const Effect& process);

    // Turns a stereo clip into mono: sourceChannel 0/1 keeps that channel,
    // -1 averages both. No-op on a mono clip.
    bool makeMono(int sourceChannel);

    // Snapshot the current state as an undo step. The edits above do this
    // themselves; the recorder calls it before a new take is appended so the
    // take can be undone as a whole.
    void pushUndoState();

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    bool undo();
    bool redo();

    // Writes the clip as 24-bit audio; .flac by extension, otherwise WAV.
    bool writeToFile(const juce::File& file, juce::String& errorMessage) const;

    SampleRange clampRange(SampleRange range) const;

private:
    struct Snapshot
    {
        juce::AudioBuffer<float> audio; // exactly numSamples long
        double sampleRate = 0.0;
    };

    Snapshot makeSnapshot() const;
    void restoreSnapshot(Snapshot&& snapshot);
    void trimUndoHistory();
    void ensureCapacity(juce::int64 samplesNeeded);
    void rebuildPeaks();
    void updatePeaksFrom(juce::int64 firstSample);
    void changed() { ++revision; }

    mutable juce::CriticalSection lock;
    juce::AudioBuffer<float> buffer;
    juce::int64 numSamples = 0;
    double sampleRate = 0.0;
    std::vector<std::vector<PeakBin>> peaks;
    juce::uint32 revision = 0;

    // Undo snapshots are whole-clip copies, so they're capped by total size
    // rather than by count -- a long take would otherwise eat gigabytes.
    std::vector<Snapshot> undoStack, redoStack;
    static constexpr size_t maxUndoBytes = (size_t) 512 * 1024 * 1024;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicClip)
};
