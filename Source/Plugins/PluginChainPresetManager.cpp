#include "PluginChainPresetManager.h"
#include "PluginChain.h"

PluginChainPresetManager::PluginChainPresetManager()
{
    loadFromDisk();
}

void PluginChainPresetManager::savePreset(const juce::String& name, const PluginChain& chain)
{
    auto state = chain.getState();
    for (auto& p : presets)
    {
        if (p.name == name)
        {
            p.chainState = *state;
            saveToDisk();
            return;
        }
    }
    presets.push_back({ name, *state });
    saveToDisk();
}

bool PluginChainPresetManager::deletePreset(const juce::String& name)
{
    for (auto it = presets.begin(); it != presets.end(); ++it)
    {
        if (it->name == name)
        {
            presets.erase(it);
            saveToDisk();
            return true;
        }
    }
    return false;
}

bool PluginChainPresetManager::applyPreset(const juce::String& name, PluginChain& chain,
                                           PluginHostManager& hostManager) const
{
    for (auto& p : presets)
    {
        if (p.name == name)
        {
            chain.restoreState(p.chainState, hostManager);
            return true;
        }
    }
    return false;
}

juce::File PluginChainPresetManager::getPresetsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("SoundPlayer2")
               .getChildFile("fx_chain_presets.xml");
}

void PluginChainPresetManager::loadFromDisk()
{
    auto file = getPresetsFile();
    if (!file.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(file);
    if (!xml || xml->getTagName() != "PluginChainPresets")
        return;

    for (auto* child : xml->getChildIterator())
    {
        if (child->getTagName() != "Preset")
            continue;

        auto name = child->getStringAttribute("name");
        if (name.isEmpty())
            continue;

        auto* chainXml = child->getChildByName("PluginChain");
        if (chainXml == nullptr)
            continue;

        presets.push_back({ name, *chainXml });
    }
}

void PluginChainPresetManager::saveToDisk() const
{
    auto file = getPresetsFile();
    file.getParentDirectory().createDirectory();

    juce::XmlElement root("PluginChainPresets");
    for (auto& p : presets)
    {
        auto* child = root.createNewChildElement("Preset");
        child->setAttribute("name", p.name);
        child->addChildElement(new juce::XmlElement(p.chainState));
    }
    root.writeTo(file);
}
