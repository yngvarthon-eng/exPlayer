#pragma once

#include <juce_core/juce_core.h>
#include <vector>

class PluginChain;
class PluginHostManager;

struct PluginChainPreset
{
    juce::String name;
    juce::XmlElement chainState { "PluginChain" }; // as produced by PluginChain::getState()
};

/**
 * Named FX-chain snapshots (which plugins, order, bypass, and per-plugin parameter
 * state), persisted to disk. Mirrors EqPresetManager's file convention. Unlike EQ,
 * there are no built-in presets — chain contents are entirely dependent on which
 * plugins the user has installed.
 */
class PluginChainPresetManager
{
public:
    PluginChainPresetManager();

    const std::vector<PluginChainPreset>& getPresets() const { return presets; }

    void savePreset(const juce::String& name, const PluginChain& chain);
    bool deletePreset(const juce::String& name);

    // Replaces chain's contents with the named preset. Returns false if not found.
    bool applyPreset(const juce::String& name, PluginChain& chain, PluginHostManager& hostManager) const;

    void loadFromDisk();
    void saveToDisk() const;

private:
    std::vector<PluginChainPreset> presets;

    juce::File getPresetsFile() const;
};
