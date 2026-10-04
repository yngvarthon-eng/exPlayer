#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <memory>

class PluginHostManager;

/**
 * An ordered chain of AudioPluginInstances applied in series on the audio thread.
 *
 * Thread safety:
 *   - process() is called from the audio thread.
 *   - addPlugin/removePlugin/movePlugin are called from the GUI thread.
 *   - Modifications take effect at the next audio callback via an atomic swap.
 */
class PluginChain
{
public:
    PluginChain() = default;
    ~PluginChain() = default;

    // Called from audio thread before playback starts.
    void prepare(double sampleRate, int maxBlockSize);

    // Called from audio thread each callback — processes buffer in place.
    // midiMessages carries whatever MIDI (on-screen keyboard, hardware input) the
    // caller collected for this block; plugins that accept MIDI (e.g. hosted synths)
    // receive it, and any messages plugins generate remain in the buffer afterwards.
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

    // GUI-thread mutations — each call acquires the lock briefly.
    void addPlugin(std::unique_ptr<juce::AudioPluginInstance> instance);
    void removePlugin(int index);
    void movePlugin(int fromIndex, int toIndex);
    void setBypass(int index, bool bypassed);

    // When on, this plugin processes into a private scratch buffer instead of the
    // chain's main signal path: its output becomes the *next* sidechain-capable
    // plugin's carrier input (see SidechainReceiver), but the main buffer (e.g.
    // your actual track audio) passes through this stage completely untouched.
    // Meant for a synth acting purely as a carrier generator ahead of something
    // like a vocoder — without this, a synth's processBlock() typically clears and
    // overwrites the whole buffer, destroying the very program audio a later
    // plugin's primary input needs.
    void setSidechainOnly(int index, bool sidechainOnly);
    void clear();

    // GUI-thread read-only queries — acquire lock.
    int size() const;
    juce::AudioPluginInstance* getPlugin(int index) const;
    bool isBypassed(int index) const;
    bool isSidechainOnly(int index) const;
    juce::String getPluginName(int index) const;

    // Serializes plugin descriptions, order, bypass state, and each plugin's own
    // getStateInformation() blob — used by PluginChainPersistence (whole-chain
    // save/restore across app restarts) and PluginChainPresetManager (named snapshots).
    std::unique_ptr<juce::XmlElement> getState() const;

    // Replaces the current chain's contents with what's in `state` (as produced by
    // getState()), re-instantiating each plugin via hostManager. A plugin that fails
    // to load (e.g. no longer installed) is skipped rather than failing the whole
    // restore. Must be called after prepare() so plugins get the right sample rate.
    void restoreState(const juce::XmlElement& state, PluginHostManager& hostManager);

private:
    struct Entry
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        bool bypassed = false;
        bool sidechainOnly = false;
    };

    mutable juce::CriticalSection lock;
    std::vector<Entry> plugins;

    // "Whatever the previous stage produced" -- see SidechainReceiver. Reused
    // block-to-block (resized once, not reallocated) purely as audio-thread scratch
    // space; never touched from the GUI thread.
    juce::AudioBuffer<float> previousStageBuffer;

    // Private working buffer a sidechain-only plugin processes into, so its output
    // can feed previousStageBuffer without ever touching the chain's main buffer.
    juce::AudioBuffer<float> sidechainScratchBuffer;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChain)
};
