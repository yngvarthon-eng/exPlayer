#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include "MicClip.h"

// Captures a microphone (any audio input) into a MicClip.
//
// It owns its own input-only AudioDeviceManager instead of adding inputs to
// AudioEngine's: choosing or failing to open a mic then never restarts or
// breaks the playback device, and the mic is only held open while the
// recorder window exists. On Linux the default choice is ALSA's "default"
// input, which goes through PipeWire/PulseAudio -- the raw hw: devices are
// usually already held by the sound server and refuse a second opener.
//
// Most PC mic jacks are stereo but most mics are mono, so the signal arrives
// on one channel and the other carries only noise. ChannelMode::AutoMono
// (the default) records just the channel that has signal, decided from the
// input levels when Record is pressed.
//
// The audio thread writes into a lock-free FIFO; the message thread calls
// drainInto() regularly to move what has arrived into the clip.
class MicRecorder : public juce::AudioIODeviceCallback,
                    private juce::ChangeListener
{
public:
    enum class ChannelMode { AutoMono = 0, Left, Right, Stereo };

    MicRecorder();
    ~MicRecorder() override;

    // Opens the previously chosen input (or the system default). Returns an
    // error message, empty on success.
    juce::String openInputDevice();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    // e.g. "Default ALSA Input - 48000 Hz, 2 ch", or empty if nothing is open.
    juce::String describeCurrentInput() const;
    bool hasUsableInput() const;
    double getInputSampleRate() const;

    // Starts appending captured audio. Fails (with a reason) if no input is open.
    bool startRecording(juce::String& errorMessage);
    // Stops capture and waits out any in-flight audio callback, so a final
    // drainInto() afterwards gets every recorded sample.
    void stopRecording();
    bool isRecording() const { return recording.load(); }

    // Takes effect at the next startRecording().
    void setChannelMode(ChannelMode mode);
    ChannelMode getChannelMode() const { return channelMode.load(); }
    // What the current (or, when idle, the next) take records from, e.g.
    // "right channel" -- for the UI.
    juce::String describeRecordSource() const;

    // Moves captured audio into clip. Returns the number of samples appended.
    int drainInto(MicClip& clip);

    // Peak level per input channel since the last call (0..1+), for the meter.
    // Updates whether or not a recording is running.
    float takePeak(int channel);
    int getNumInputChannels() const { return captureChannels.load(); }

    // Sizes the FIFO for a capture format. Run automatically whenever the
    // device (re)starts; public so tests can drive the callback without a device.
    void prepareCapture(double sampleRate, int numChannels);

    // Samples lost because the message thread didn't drain in time.
    juce::int64 getDroppedSamples() const { return droppedSamples.load(); }

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override; // device changed -> save it
    static juce::File getSettingsFile();

    // Source of a take: 0/1 = that input channel, or one of these.
    static constexpr int mixSource = -1;
    static constexpr int stereoSource = -2;
    int resolveSource() const;

    juce::AudioDeviceManager deviceManager;

    static constexpr int maxChannels = 2;
    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> fifoBuffer;
    juce::AudioBuffer<float> drainBuffer;

    std::atomic<bool> recording { false };
    std::atomic<int> captureChannels { 0 };
    std::atomic<double> captureSampleRate { 0.0 };
    std::atomic<float> peaks[maxChannels] {};
    std::atomic<float> meanSquares[maxChannels] {};
    std::atomic<ChannelMode> channelMode { ChannelMode::AutoMono };
    std::atomic<int> recordSource { 0 };
    std::atomic<int> recordChannels { 1 };
    std::atomic<juce::int64> droppedSamples { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicRecorder)
};
