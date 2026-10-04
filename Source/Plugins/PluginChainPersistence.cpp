#include "PluginChainPersistence.h"
#include "PluginChain.h"

juce::File PluginChainPersistence::getChainFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("SoundPlayer2")
               .getChildFile("fx_chain.xml");
}

void PluginChainPersistence::save(const PluginChain& chain)
{
    auto file = getChainFile();
    file.getParentDirectory().createDirectory();
    chain.getState()->writeTo(file);
}

bool PluginChainPersistence::load(PluginChain& chain, PluginHostManager& hostManager)
{
    auto file = getChainFile();
    if (!file.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return false;

    chain.restoreState(*xml, hostManager);
    return true;
}
