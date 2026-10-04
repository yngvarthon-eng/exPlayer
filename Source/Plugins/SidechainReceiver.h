#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/**
 * Optional interface a hosted AudioPluginInstance subclass can implement to
 * receive a sidechain input from PluginChain — the previous stage's output —
 * before its own processBlock() runs.
 *
 * Exists for plugins with audio-input ports beyond the main channel count (e.g.
 * a vocoder's carrier/sidechain input): PluginChain checks for this interface via
 * dynamic_cast, so it works for any format without PluginChain needing to know
 * about that format specifically.
 */
class SidechainReceiver
{
public:
    virtual ~SidechainReceiver() = default;

    // previousStageOutput is the chain's buffer as it stood right before this
    // plugin's processBlock() — i.e. whatever the plugin before it produced, or
    // (for the first plugin, or a plugin used alone) the untouched incoming
    // program audio, so a lone sidechain-hungry plugin still falls back to using
    // its own main input rather than getting silence.
    virtual void setSidechainInput(const juce::AudioBuffer<float>& previousStageOutput) = 0;
};
