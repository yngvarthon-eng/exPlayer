#include "MicRecorder.h"
#include "ClipEffects.h"

namespace
{
    // How much audio the FIFO holds between drains. The UI drains at ~30 Hz,
    // so this only matters if the message thread stalls (a modal dialog, a
    // slow repaint) -- four seconds rides out anything short of a hang.
    constexpr double fifoSeconds = 4.0;
}

MicRecorder::MicRecorder()
{
    deviceManager.addChangeListener(this);
}

MicRecorder::~MicRecorder()
{
    recording.store(false);
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
}

juce::File MicRecorder::getSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("SoundPlayer2")
               .getChildFile("mic_input.xml");
}

juce::String MicRecorder::openInputDevice()
{
    std::unique_ptr<juce::XmlElement> saved;
    if (getSettingsFile().existsAsFile())
        saved = juce::XmlDocument::parse(getSettingsFile());

    // Inputs only: up to two channels, no outputs at all.
    auto error = deviceManager.initialise(maxChannels, 0, saved.get(), true);
    deviceManager.addAudioCallback(this);

    if (error.isEmpty() && ! hasUsableInput())
        error = "No audio input device was found.";

    return error;
}

void MicRecorder::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Only explicit choices from the device dialog produce state worth saving.
    if (auto state = deviceManager.createStateXml())
    {
        getSettingsFile().getParentDirectory().createDirectory();
        state->writeTo(getSettingsFile());
    }
}

bool MicRecorder::hasUsableInput() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr && device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

double MicRecorder::getInputSampleRate() const
{
    return captureSampleRate.load();
}

juce::String MicRecorder::describeCurrentInput() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return {};

    // ALSA's "default" PCM describes itself as an output even when opened for
    // capture ("Default ALSA Output (currently PipeWire Media Server)").
    auto name = device->getName().replace("Default ALSA Output", "System default input");

    const int channels = device->getActiveInputChannels().countNumberOfSetBits();
    return name + "  -  " + juce::String(device->getCurrentSampleRate(), 0) + " Hz, "
         + (channels == 1 ? juce::String("mono") : juce::String(channels) + " ch");
}

void MicRecorder::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    const int channels = juce::jlimit(0, maxChannels, device->getActiveInputChannels().countNumberOfSetBits());
    prepareCapture(device->getCurrentSampleRate(), channels);
}

void MicRecorder::prepareCapture(double sampleRate, int numChannels)
{
    // Called before callbacks (re)start and from the message thread, which is
    // also the only reader -- so nothing is touching the FIFO right now.
    const int size = juce::jmax(4096, (int) (sampleRate * fifoSeconds));
    fifoBuffer.setSize(maxChannels, size);
    drainBuffer.setSize(maxChannels, size);
    fifo.setTotalSize(size);
    fifo.reset();

    for (auto& level : meanSquares)
        level.store(0.0f);
    captureChannels.store(numChannels);
    captureSampleRate.store(sampleRate);
}

void MicRecorder::audioDeviceStopped()
{
    recording.store(false);
    captureChannels.store(0);
}

int MicRecorder::resolveSource() const
{
    if (captureChannels.load() < 2)
        return 0; // a mono device: there is only one source

    switch (channelMode.load())
    {
        case ChannelMode::Left:   return 0;
        case ChannelMode::Right:  return 1;
        case ChannelMode::Stereo: return stereoSource;
        case ChannelMode::AutoMono:
        default:
            return ClipEffects::chooseMonoSource(meanSquares[0].load(), meanSquares[1].load());
    }
}

bool MicRecorder::startRecording(juce::String& errorMessage)
{
    // captureChannels is only non-zero while a device with inputs is running.
    if (captureChannels.load() == 0)
    {
        errorMessage = "No microphone input is open. Pick one with \"Input device...\".";
        return false;
    }

    // Discard whatever trickled in before Record was pressed (drain side only,
    // so this is safe while the audio thread runs).
    {
        const auto discard = fifo.read(fifo.getNumReady()); // consumed when it goes out of scope
    }

    // Fixed for the whole take, so a pause in speech can't flip the auto choice.
    const int source = resolveSource();
    recordSource.store(source);
    recordChannels.store(source == stereoSource ? 2 : 1);

    droppedSamples.store(0);
    recording.store(true);
    return true;
}

juce::String MicRecorder::describeRecordSource() const
{
    if (captureChannels.load() < 2)
        return "mono input";

    switch (isRecording() ? recordSource.load() : resolveSource())
    {
        case 0:            return "left channel";
        case 1:            return "right channel";
        case mixSource:    return "left + right mixed";
        case stereoSource: return "stereo";
        default:           return {};
    }
}

void MicRecorder::setChannelMode(ChannelMode mode)
{
    channelMode.store(mode);
}

void MicRecorder::stopRecording()
{
    recording.store(false);

    // AudioDeviceManager holds this lock for the whole of each callback, so
    // taking it waits out a block that saw recording == true just before.
    const juce::ScopedLock sl(deviceManager.getAudioCallbackLock());
}

int MicRecorder::drainInto(MicClip& clip)
{
    const int ready = fifo.getNumReady();
    if (ready <= 0)
        return 0;

    const int channels = recordChannels.load();
    {
        const auto scope = fifo.read(ready);
        if (scope.blockSize1 > 0)
            for (int ch = 0; ch < channels; ++ch)
                drainBuffer.copyFrom(ch, 0, fifoBuffer, ch, scope.startIndex1, scope.blockSize1);
        if (scope.blockSize2 > 0)
            for (int ch = 0; ch < channels; ++ch)
                drainBuffer.copyFrom(ch, scope.blockSize1, fifoBuffer, ch, scope.startIndex2, scope.blockSize2);
    }

    // A view of just the channels this take records.
    juce::AudioBuffer<float> take(drainBuffer.getArrayOfWritePointers(), channels, ready);
    return clip.append(take, ready, captureSampleRate.load()) ? ready : 0;
}

float MicRecorder::takePeak(int channel)
{
    if (channel < 0 || channel >= maxChannels)
        return 0.0f;
    return peaks[channel].exchange(0.0f);
}

void MicRecorder::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                   int numInputChannels,
                                                   float* const* outputChannelData,
                                                   int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    // Input-only device, but be tidy if a backend hands us outputs anyway.
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);

    const int channels = juce::jmin(numInputChannels, captureChannels.load(), maxChannels);
    for (int ch = 0; ch < channels; ++ch)
        if (inputChannelData[ch] == nullptr)
            return;

    for (int ch = 0; ch < channels; ++ch)
    {
        const float* in = inputChannelData[ch];
        const auto range = juce::FloatVectorOperations::findMinAndMax(in, numSamples);
        const float peak = juce::jmax(range.getEnd(), -range.getStart());
        if (peak > peaks[ch].load())
            peaks[ch].store(peak);

        // ~0.5 s moving level, for AutoMono's choice of channel.
        float sumSquares = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            sumSquares += in[i] * in[i];
        const float blockMeanSquare = sumSquares / (float) juce::jmax(1, numSamples);
        meanSquares[ch].store(meanSquares[ch].load() * 0.95f + blockMeanSquare * 0.05f);
    }

    if (! recording.load() || channels == 0)
        return;

    const int space = fifo.getFreeSpace();
    const int toWrite = juce::jmin(space, numSamples);
    if (toWrite < numSamples)
        droppedSamples.fetch_add(numSamples - toWrite);

    const int source = recordSource.load();
    const auto scope = fifo.write(toWrite);

    auto writeChannel = [&](int fifoChannel, const float* in, float gain, bool add)
    {
        if (scope.blockSize1 > 0)
        {
            if (add) fifoBuffer.addFrom (fifoChannel, scope.startIndex1, in, scope.blockSize1, gain);
            else     fifoBuffer.copyFrom(fifoChannel, scope.startIndex1, in, scope.blockSize1, gain);
        }
        if (scope.blockSize2 > 0)
        {
            if (add) fifoBuffer.addFrom (fifoChannel, scope.startIndex2, in + scope.blockSize1, scope.blockSize2, gain);
            else     fifoBuffer.copyFrom(fifoChannel, scope.startIndex2, in + scope.blockSize1, scope.blockSize2, gain);
        }
    };

    if (source == stereoSource)
    {
        writeChannel(0, inputChannelData[0], 1.0f, false);
        writeChannel(1, inputChannelData[juce::jmin(1, channels - 1)], 1.0f, false);
    }
    else if (source == mixSource && channels >= 2)
    {
        writeChannel(0, inputChannelData[0], 0.5f, false);
        writeChannel(0, inputChannelData[1], 0.5f, true);
    }
    else
    {
        writeChannel(0, inputChannelData[juce::jlimit(0, channels - 1, source)], 1.0f, false);
    }
}
