#pragma once

#include <juce_core/juce_core.h>

class PluginChain;
class PluginHostManager;

/**
 * Persists the FX chain's current contents (plugins, order, bypass, and each
 * plugin's own parameter state) across app restarts.
 *
 * Mirrors EqPresetManager's file convention: same base directory
 * (~/.local/share/SoundPlayer2 on Linux), save-immediately-after-every-mutation
 * for crash resilience.
 */
namespace PluginChainPersistence
{
    juce::File getChainFile();

    void save(const PluginChain& chain);

    // Returns false if there was nothing to load (no file, or it failed to parse).
    bool load(PluginChain& chain, PluginHostManager& hostManager);
}
