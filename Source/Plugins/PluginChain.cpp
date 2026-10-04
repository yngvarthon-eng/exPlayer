#include "PluginChain.h"
#include "PluginHostManager.h"
#include "SidechainReceiver.h"

void PluginChain::prepare(double sampleRate, int maxBlockSize)
{
    juce::ScopedLock sl(lock);
    currentSampleRate = sampleRate;
    currentBlockSize  = maxBlockSize;

    for (auto& e : plugins)
        e.instance->prepareToPlay(sampleRate, maxBlockSize);
}

void PluginChain::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedTryLock stl(lock);
    if (!stl.isLocked())
        return; // drop the frame rather than block the audio thread

    // Starts as the untouched incoming program audio, so a lone sidechain-hungry
    // plugin (nothing before it in the chain) falls back to using its own main
    // input rather than getting silence; updated to each plugin's own output
    // afterward so the *next* plugin's sidechain sees what actually preceded it.
    previousStageBuffer.makeCopyOf(buffer, true);

    for (auto& e : plugins)
    {
        if (e.bypassed)
            continue;

        if (e.sidechainOnly)
        {
            // Process into a private scratch buffer instead of the main signal
            // path: this plugin's output becomes the next sidechain-capable
            // plugin's carrier input, but the main buffer (e.g. the actual track
            // audio) passes through this stage completely untouched -- otherwise
            // a synth's processBlock() typically clears and overwrites the whole
            // buffer, destroying the very program audio a later plugin's primary
            // input needs.
            sidechainScratchBuffer.makeCopyOf(buffer, true);
            e.instance->processBlock(sidechainScratchBuffer, midiMessages);
            previousStageBuffer.makeCopyOf(sidechainScratchBuffer, true);
            continue;
        }

        if (auto* sidechainReceiver = dynamic_cast<SidechainReceiver*>(e.instance.get()))
            sidechainReceiver->setSidechainInput(previousStageBuffer);

        e.instance->processBlock(buffer, midiMessages);

        previousStageBuffer.makeCopyOf(buffer, true);
    }
}

void PluginChain::addPlugin(std::unique_ptr<juce::AudioPluginInstance> instance)
{
    instance->prepareToPlay(currentSampleRate, currentBlockSize);
    juce::ScopedLock sl(lock);
    plugins.push_back({ std::move(instance), false });
}

void PluginChain::removePlugin(int index)
{
    juce::ScopedLock sl(lock);
    if (index >= 0 && index < (int)plugins.size())
    {
        plugins[index].instance->releaseResources();
        plugins.erase(plugins.begin() + index);
    }
}

void PluginChain::movePlugin(int fromIndex, int toIndex)
{
    juce::ScopedLock sl(lock);
    if (fromIndex < 0 || fromIndex >= (int)plugins.size())
        return;
    toIndex = juce::jlimit(0, (int)plugins.size() - 1, toIndex);
    if (fromIndex == toIndex)
        return;

    auto entry = std::move(plugins[fromIndex]);
    plugins.erase(plugins.begin() + fromIndex);
    plugins.insert(plugins.begin() + toIndex, std::move(entry));
}

void PluginChain::setBypass(int index, bool bypassed)
{
    juce::ScopedLock sl(lock);
    if (index >= 0 && index < (int)plugins.size())
        plugins[index].bypassed = bypassed;
}

void PluginChain::setSidechainOnly(int index, bool sidechainOnly)
{
    juce::ScopedLock sl(lock);
    if (index >= 0 && index < (int)plugins.size())
        plugins[index].sidechainOnly = sidechainOnly;
}

void PluginChain::clear()
{
    juce::ScopedLock sl(lock);
    for (auto& e : plugins)
        e.instance->releaseResources();
    plugins.clear();
}

int PluginChain::size() const
{
    juce::ScopedLock sl(lock);
    return (int)plugins.size();
}

juce::AudioPluginInstance* PluginChain::getPlugin(int index) const
{
    juce::ScopedLock sl(lock);
    if (index >= 0 && index < (int)plugins.size())
        return plugins[index].instance.get();
    return nullptr;
}

bool PluginChain::isBypassed(int index) const
{
    juce::ScopedLock sl(lock);
    if (index >= 0 && index < (int)plugins.size())
        return plugins[index].bypassed;
    return false;
}

bool PluginChain::isSidechainOnly(int index) const
{
    juce::ScopedLock sl(lock);
    if (index >= 0 && index < (int)plugins.size())
        return plugins[index].sidechainOnly;
    return false;
}

juce::String PluginChain::getPluginName(int index) const
{
    juce::ScopedLock sl(lock);
    if (index >= 0 && index < (int)plugins.size())
        return plugins[index].instance->getName();
    return {};
}

std::unique_ptr<juce::XmlElement> PluginChain::getState() const
{
    juce::ScopedLock sl(lock);

    auto root = std::make_unique<juce::XmlElement>("PluginChain");
    for (auto& e : plugins)
    {
        juce::PluginDescription desc;
        e.instance->fillInPluginDescription(desc);

        juce::MemoryBlock stateBlock;
        e.instance->getStateInformation(stateBlock);

        auto* entry = root->createNewChildElement("Plugin");
        entry->setAttribute("bypassed", e.bypassed);
        entry->setAttribute("sidechainOnly", e.sidechainOnly);
        entry->setAttribute("state", stateBlock.toBase64Encoding());
        entry->addChildElement(desc.createXml().release());
    }
    return root;
}

void PluginChain::restoreState(const juce::XmlElement& state, PluginHostManager& hostManager)
{
    if (state.getTagName() != "PluginChain")
        return;

    clear();

    for (auto* entry : state.getChildIterator())
    {
        if (entry->getTagName() != "Plugin")
            continue;

        auto* descXml = entry->getChildByName("PLUGIN");
        if (descXml == nullptr)
            continue;

        juce::PluginDescription desc;
        if (!desc.loadFromXml(*descXml))
            continue;

        juce::String error;
        auto instance = hostManager.loadPlugin(desc, currentSampleRate, currentBlockSize, error);
        if (instance == nullptr)
            continue; // no longer installed, or failed to load — skip rather than abort the restore

        addPlugin(std::move(instance));
        const int index = size() - 1;

        juce::MemoryBlock stateBlock;
        if (stateBlock.fromBase64Encoding(entry->getStringAttribute("state")) && stateBlock.getSize() > 0)
            if (auto* loaded = getPlugin(index))
                loaded->setStateInformation(stateBlock.getData(), (int)stateBlock.getSize());

        setBypass(index, entry->getBoolAttribute("bypassed", false));
        setSidechainOnly(index, entry->getBoolAttribute("sidechainOnly", false));
    }
}
