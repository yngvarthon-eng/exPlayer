#ifdef SOUNDPLAYER_LV2_SUPPORT

#include "Lv2PluginFormat.h"
#include "SidechainReceiver.h"

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// LV2 URIs used for port type detection
// ---------------------------------------------------------------------------
static const char* LV2_AUDIO_PORT_URI  = "http://lv2plug.in/ns/lv2core#AudioPort";
static const char* LV2_CONTROL_PORT_URI= "http://lv2plug.in/ns/lv2core#ControlPort";
static const char* LV2_ATOM_PORT_URI   = "http://lv2plug.in/ns/ext/atom#AtomPort";
static const char* LV2_INPUT_PORT_URI  = "http://lv2plug.in/ns/lv2core#InputPort";
static const char* LV2_OUTPUT_PORT_URI = "http://lv2plug.in/ns/lv2core#OutputPort";

// ---------------------------------------------------------------------------
// Minimal LV2 Atom Sequence writer for MIDI events, built by hand against the
// documented binary layout (LV2_Atom{size,type} + LV2_Atom_Sequence_Body{unit,pad}
// + a run of LV2_Atom_Event, each 64-bit aligned) rather than the forge helper API,
// so the wire format is fully explicit and easy to verify against the spec.
// ---------------------------------------------------------------------------
namespace
{
    struct Lv2AtomHeader { std::uint32_t size; std::uint32_t type; };
    struct Lv2SeqBody    { std::uint32_t unit; std::uint32_t pad; };

    // Fills buf with a valid, empty atom:Sequence whose declared size equals the
    // buffer's remaining capacity — required so the plugin knows how much room it
    // has to write into an *output* atom port without the host ever reading it back.
    void initEmptyAtomSequence(std::vector<std::uint8_t>& buf, LV2_URID sequenceUrid)
    {
        Lv2AtomHeader header { (std::uint32_t)(buf.size() - sizeof(Lv2AtomHeader)), sequenceUrid };
        Lv2SeqBody body { 0, 0 };
        std::memcpy(buf.data(), &header, sizeof(header));
        std::memcpy(buf.data() + sizeof(header), &body, sizeof(body));
    }

    // Encodes every message in midiMessages as a frame-time-tagged MIDI event into
    // buf, in the exact layout an atom:Sequence input port expects. Events that
    // don't fit are dropped rather than overflowing the buffer.
    void buildMidiAtomSequence(std::vector<std::uint8_t>& buf, LV2_URID sequenceUrid,
                               LV2_URID midiEventUrid, const juce::MidiBuffer& midiMessages)
    {
        size_t offset = sizeof(Lv2AtomHeader) + sizeof(Lv2SeqBody);

        for (const auto metadata : midiMessages)
        {
            const auto message = metadata.getMessage();
            const int size = message.getRawDataSize();
            if (size <= 0 || size > 64) // guard against sysex/oversized data we don't expect here
                continue;

            const size_t eventBytes = sizeof(std::int64_t) + sizeof(Lv2AtomHeader) + (size_t)size;
            const size_t paddedEventBytes = (eventBytes + 7u) & ~size_t(7); // 64-bit align
            if (offset + paddedEventBytes > buf.size())
                break; // out of room -- drop remaining events rather than overflow

            const std::int64_t frames = (std::int64_t)metadata.samplePosition;
            std::memcpy(buf.data() + offset, &frames, sizeof(frames));
            offset += sizeof(frames);

            const Lv2AtomHeader eventHeader { (std::uint32_t)size, midiEventUrid };
            std::memcpy(buf.data() + offset, &eventHeader, sizeof(eventHeader));
            offset += sizeof(eventHeader);

            std::memcpy(buf.data() + offset, message.getRawData(), (size_t)size);
            offset += (size_t)size;

            const size_t pad = paddedEventBytes - (sizeof(frames) + sizeof(eventHeader) + (size_t)size);
            if (pad > 0)
            {
                static const std::uint8_t zeros[8] = { 0 };
                std::memcpy(buf.data() + offset, zeros, pad);
                offset += pad;
            }
        }

        const Lv2AtomHeader header { (std::uint32_t)(offset - sizeof(Lv2AtomHeader)), sequenceUrid };
        const Lv2SeqBody body { 0, 0 };
        std::memcpy(buf.data(), &header, sizeof(header));
        std::memcpy(buf.data() + sizeof(header), &body, sizeof(body));
    }
}

// ---------------------------------------------------------------------------
// Pimpl — owns the LilvWorld
// ---------------------------------------------------------------------------
struct Lv2PluginFormat::Pimpl
{
    LilvWorld* world = nullptr;

    // Minimal urid:map/urid:unmap host feature, shared by every plugin instance
    // this format creates — required for any plugin with an Atom port (e.g. MIDI
    // in/out) to instantiate at all; without it, lilv_plugin_instantiate() fails
    // outright for such plugins. IDs are assigned in first-seen order starting at
    // 1 (0 is reserved/invalid per the LV2 spec).
    std::vector<std::string> uridToUri;
    std::unordered_map<std::string, LV2_URID> uriToUrid;

    LV2_URID mapUri(const char* uri)
    {
        auto it = uriToUrid.find(uri);
        if (it != uriToUrid.end())
            return it->second;
        uridToUri.emplace_back(uri);
        const auto id = (LV2_URID)uridToUri.size(); // 1-based
        uriToUrid[uri] = id;
        return id;
    }

    const char* unmapUrid(LV2_URID urid) const
    {
        if (urid == 0 || urid > uridToUri.size())
            return nullptr;
        return uridToUri[(size_t)urid - 1].c_str();
    }

    static LV2_URID mapCallback(LV2_URID_Map_Handle handle, const char* uri)
    {
        return static_cast<Pimpl*>(handle)->mapUri(uri);
    }
    static const char* unmapCallback(LV2_URID_Unmap_Handle handle, LV2_URID urid)
    {
        return static_cast<Pimpl*>(handle)->unmapUrid(urid);
    }

    LV2_URID_Map   uridMapData   { this, mapCallback };
    LV2_URID_Unmap uridUnmapData { this, unmapCallback };
    LV2_Feature    uridMapFeature   { LV2_URID__map,   &uridMapData };
    LV2_Feature    uridUnmapFeature { LV2_URID__unmap, &uridUnmapData };
    const LV2_Feature* features[3] { &uridMapFeature, &uridUnmapFeature, nullptr };

    // Precomputed URIDs used every audio block to build MIDI atom sequences.
    LV2_URID midiEventUrid = 0;
    LV2_URID sequenceUrid  = 0;

    Pimpl()
    {
        world = lilv_world_new();
        lilv_world_load_all(world);
        midiEventUrid = mapUri(LV2_MIDI__MidiEvent);
        sequenceUrid  = mapUri(LV2_ATOM__Sequence);
    }

    ~Pimpl()
    {
        lilv_world_free(world);
    }
};

// ---------------------------------------------------------------------------
// Shared PluginDescription construction — used for both URI-identifier lookups
// and bundle-directory scanning, so the two paths can't drift out of sync.
// ---------------------------------------------------------------------------
static juce::PluginDescription* createDescriptionForPlugin(const LilvPlugin* p)
{
    const LilvNode* uriNode = lilv_plugin_get_uri(p);
    LilvNode* nameNode   = lilv_plugin_get_name(p);
    LilvNode* authorNode = lilv_plugin_get_author_name(p);

    auto* pd = new juce::PluginDescription();
    pd->name                = nameNode  ? lilv_node_as_string(nameNode)  : lilv_node_as_string(uriNode);
    pd->descriptiveName     = pd->name;
    pd->pluginFormatName    = "LV2";
    pd->manufacturerName    = authorNode ? lilv_node_as_string(authorNode) : "";
    pd->fileOrIdentifier    = lilv_node_as_string(uriNode);
    pd->uniqueId            = pd->fileOrIdentifier.hashCode();
    pd->lastFileModTime     = juce::Time::getCurrentTime();
    pd->lastInfoUpdateTime  = juce::Time::getCurrentTime();
    pd->numInputChannels    = 2;
    pd->numOutputChannels   = 2;
    pd->isInstrument        = false;

    lilv_node_free(nameNode);
    lilv_node_free(authorNode);
    return pd;
}

// ---------------------------------------------------------------------------
// One exposed parameter per LV2 control-input port. AudioPluginInstance requires
// all its parameters to be HostedAudioProcessorParameters (plain AudioParameterFloat
// is rejected — addParameter() is made private specifically to enforce this), which
// only needs getValue()/setValue() in normalised 0-1 terms plus a stable ID; both
// read/write `*value` directly, so there's no separate cached state to keep in sync
// with the float actually connected to the LV2 port.
// ---------------------------------------------------------------------------
class Lv2ControlParameter : public juce::HostedAudioProcessorParameter
{
public:
    Lv2ControlParameter(juce::String parameterId, juce::String parameterName,
                        float* valueSlot, float lo, float hi, float defaultDenormalised)
        : id(std::move(parameterId)), name(std::move(parameterName)),
          value(valueSlot), minValue(lo), maxValue(hi), defaultValue(defaultDenormalised)
    {
    }

    juce::String getParameterID() const override { return id; }

    float getValue() const override { return normalise(*value); }
    void setValue(float newValue) override { *value = denormalise(juce::jlimit(0.0f, 1.0f, newValue)); }
    float getDefaultValue() const override { return normalise(defaultValue); }

    juce::String getName(int maximumStringLength) const override
    {
        return name.substring(0, maximumStringLength);
    }
    juce::String getLabel() const override { return {}; }
    float getValueForText(const juce::String& text) const override
    {
        return normalise(text.getFloatValue());
    }
    juce::String getText(float normalisedValue, int) const override
    {
        return juce::String(denormalise(normalisedValue), 3);
    }

private:
    float normalise(float denorm) const
    {
        return maxValue > minValue ? (denorm - minValue) / (maxValue - minValue) : 0.0f;
    }
    float denormalise(float norm) const { return minValue + norm * (maxValue - minValue); }

    juce::String id, name;
    float* value;
    float minValue, maxValue, defaultValue;
};

// ---------------------------------------------------------------------------
// LV2 plugin instance
// ---------------------------------------------------------------------------
class Lv2PluginInstance : public juce::AudioPluginInstance,
                          public SidechainReceiver
{
public:
    Lv2PluginInstance(Lv2PluginFormat::Pimpl* format,
                      const LilvPlugin* plugin,
                      const juce::PluginDescription& desc,
                      double sampleRate,
                      int blockSize)
        : fmt(format), lilvWorld(format->world), lilvPlugin(plugin), pluginDescription(desc)
    {
        setPlayConfigDetails(2, 2, sampleRate, blockSize);
        buildPortMap();
        buildAtomPorts();
        buildParameters();
        instantiate(sampleRate, blockSize);
    }

    ~Lv2PluginInstance() override
    {
        if (handle != nullptr)
        {
            lilv_instance_deactivate(handle);
            lilv_instance_free(handle);
        }
    }

    void fillInPluginDescription(juce::PluginDescription& d) const override { d = pluginDescription; }
    const juce::String getName() const override { return pluginDescription.name; }

    // SidechainReceiver — PluginChain calls this right before processBlock() with
    // whatever the previous stage in the chain produced (see PluginChain::process).
    // Audio-input ports beyond the main channel count (e.g. a vocoder's carrier/
    // sidechain input) are fed from this instead of aliasing onto the main buffer.
    void setSidechainInput(const juce::AudioBuffer<float>& previousStageOutput) override
    {
        sidechainBuffer.makeCopyOf(previousStageOutput, true);
    }

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override
    {
        if (handle != nullptr)
        {
            lilv_instance_deactivate(handle);
            lilv_instance_free(handle);
            handle = nullptr;
        }
        setPlayConfigDetails(2, 2, sampleRate, maximumExpectedSamplesPerBlock);
        instantiate(sampleRate, maximumExpectedSamplesPerBlock);
    }

    void releaseResources() override
    {
        if (handle != nullptr)
        {
            lilv_instance_deactivate(handle);
            lilv_instance_free(handle);
            handle = nullptr;
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
    {
        if (handle == nullptr)
            return;

        const int numSamples = buffer.getNumSamples();

        // Connect audio ports
        int audioInCount = 0, audioOutCount = 0;
        const std::uint32_t numPorts = lilv_plugin_get_num_ports(lilvPlugin);
        for (std::uint32_t i = 0; i < numPorts; ++i)
        {
            const LilvPort* port = lilv_plugin_get_port_by_index(lilvPlugin, i);
            if (isAudioPort(port))
            {
                if (isInputPort(port))
                {
                    if (audioInCount < buffer.getNumChannels())
                    {
                        // Primary input (e.g. a vocoder's modulator) — the actual
                        // audio passing through the chain.
                        lilv_instance_connect_port(handle, i, buffer.getWritePointer(audioInCount));
                    }
                    else
                    {
                        // Extra input beyond the main channel count (e.g. a
                        // vocoder's carrier/sidechain) — fed from whatever the
                        // previous stage in the chain produced (see
                        // SidechainReceiver::setSidechainInput), not aliased onto
                        // the main buffer.
                        const int sideCh = sidechainBuffer.getNumChannels() > 0
                            ? juce::jmin(audioInCount - buffer.getNumChannels(), sidechainBuffer.getNumChannels() - 1)
                            : -1;
                        lilv_instance_connect_port(handle, i,
                            sideCh >= 0 ? const_cast<float*>(sidechainBuffer.getReadPointer(sideCh))
                                       : scratchBuf.data());
                    }
                    ++audioInCount;
                }
                else
                {
                    int ch = juce::jmin(audioOutCount, buffer.getNumChannels() - 1);
                    lilv_instance_connect_port(handle, i,
                        ch >= 0 ? buffer.getWritePointer(ch) : scratchBuf.data());
                    ++audioOutCount;
                }
            }
        }

        // Feed MIDI (on-screen keyboard + hardware input, merged upstream in
        // AudioEngine) into every Atom input port as a proper atom:Sequence — this
        // is what actually lets an LV2 instrument, or an effect like Calf Vocoder
        // that drives its internal oscillator from MIDI, receive notes at all.
        for (size_t k = 0; k < atomInPortIndices.size(); ++k)
        {
            buildMidiAtomSequence(atomInBuffers[k], fmt->sequenceUrid, fmt->midiEventUrid, midiMessages);
            lilv_instance_connect_port(handle, atomInPortIndices[k], atomInBuffers[k].data());
        }

        // Output Atom ports still need a validly-initialised (empty) sequence with
        // its capacity declared, even though we don't read anything back from them.
        for (size_t k = 0; k < atomOutPortIndices.size(); ++k)
        {
            initEmptyAtomSequence(atomOutBuffers[k], fmt->sequenceUrid);
            lilv_instance_connect_port(handle, atomOutPortIndices[k], atomOutBuffers[k].data());
        }

        lilv_instance_run(handle, (std::uint32_t)numSamples);
    }

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return !atomInPortIndices.empty(); }
    bool producesMidi() const override { return !atomOutPortIndices.empty(); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override
    {
        // controlValues is indexed by port index and only meaningful at control-port
        // slots, but storing it flat keeps restore a straight memcpy-and-validate.
        destData.setSize(sizeof(std::uint32_t) + controlValues.size() * sizeof(float));
        auto* dest = static_cast<char*>(destData.getData());
        std::uint32_t count = (std::uint32_t)controlValues.size();
        memcpy(dest, &count, sizeof(count));
        if (!controlValues.empty())
            memcpy(dest + sizeof(count), controlValues.data(), controlValues.size() * sizeof(float));
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        if (data == nullptr || (size_t)sizeInBytes < sizeof(std::uint32_t))
            return;

        std::uint32_t count = 0;
        memcpy(&count, data, sizeof(count));
        const size_t available = ((size_t)sizeInBytes - sizeof(count)) / sizeof(float);
        count = (std::uint32_t)juce::jmin((size_t)count, available, controlValues.size());

        auto* src = static_cast<const char*>(data) + sizeof(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            float v;
            memcpy(&v, src + i * sizeof(float), sizeof(float));
            controlValues[i] = v;
        }
        // No separate parameter-side cache to refresh: Lv2ControlParameter::getValue()
        // reads *value (i.e. controlValues[i]) live, so a freshly opened parameter
        // panel always reflects whatever was just restored here.
    }

private:
    Lv2PluginFormat::Pimpl* fmt;
    LilvWorld*           lilvWorld;
    const LilvPlugin*    lilvPlugin;
    LilvInstance*        handle = nullptr;
    juce::PluginDescription pluginDescription;
    std::vector<float>   controlValues;
    std::vector<float>   portMinValues, portMaxValues;
    std::vector<float>   scratchBuf;

    // Atom (e.g. MIDI) ports, keyed in parallel: atomInPortIndices[k] is the LV2
    // port index whose live buffer is atomInBuffers[k], and likewise for output.
    std::vector<std::uint32_t> atomInPortIndices, atomOutPortIndices;
    std::vector<std::vector<std::uint8_t>> atomInBuffers, atomOutBuffers;
    static constexpr size_t atomBufferBytes = 8192;

    // Set by SidechainReceiver::setSidechainInput() each block; read by any audio
    // input port beyond the main channel count.
    juce::AudioBuffer<float> sidechainBuffer;

    void buildPortMap()
    {
        const std::uint32_t numPorts = lilv_plugin_get_num_ports(lilvPlugin);
        controlValues.resize(numPorts, 0.0f);
        portMinValues.resize(numPorts, 0.0f);
        portMaxValues.resize(numPorts, 1.0f);
        scratchBuf.resize(8192, 0.0f);

        // Fill default control values and their declared ranges from port metadata.
        lilv_plugin_get_port_ranges_float(lilvPlugin, portMinValues.data(), portMaxValues.data(),
                                          controlValues.data());
    }

    // Exposes each control-input port as a real AudioProcessorParameter, so the
    // generic PluginParameterPanel (and any host automation) can drive it — this
    // plugin format never has a native editor (hasEditor() is always false), so
    // without this an LV2 plugin would be completely uncontrollable once loaded.
    void buildParameters()
    {
        const std::uint32_t numPorts = lilv_plugin_get_num_ports(lilvPlugin);
        for (std::uint32_t i = 0; i < numPorts; ++i)
        {
            const LilvPort* port = lilv_plugin_get_port_by_index(lilvPlugin, i);
            if (!isControlPort(port) || !isInputPort(port))
                continue;

            LilvNode* nameNode = lilv_port_get_name(lilvPlugin, port);
            juce::String paramName = nameNode ? lilv_node_as_string(nameNode)
                                              : ("Param " + juce::String((int)i));
            if (nameNode != nullptr)
                lilv_node_free(nameNode);

            float lo = portMinValues[i];
            float hi = portMaxValues[i];
            if (!(hi > lo) || std::isnan(lo) || std::isnan(hi))
            {
                lo = 0.0f;
                hi = 1.0f;
            }
            const float def = juce::jlimit(lo, hi, controlValues[i]);

            addHostedParameter(std::make_unique<Lv2ControlParameter>(
                "lv2_port_" + juce::String((int)i), paramName, &controlValues[i], lo, hi, def));
        }
    }

    // Finds every Atom port (in and out) and allocates its live buffer. Needed
    // separately from buildPortMap()/buildParameters() since Atom ports are
    // neither audio nor control ports and carry no single float value.
    void buildAtomPorts()
    {
        const std::uint32_t numPorts = lilv_plugin_get_num_ports(lilvPlugin);
        for (std::uint32_t i = 0; i < numPorts; ++i)
        {
            const LilvPort* port = lilv_plugin_get_port_by_index(lilvPlugin, i);
            if (!isAtomPort(port))
                continue;

            if (isInputPort(port))
            {
                atomInPortIndices.push_back(i);
                atomInBuffers.emplace_back(atomBufferBytes, (std::uint8_t)0);
            }
            else
            {
                atomOutPortIndices.push_back(i);
                atomOutBuffers.emplace_back(atomBufferBytes, (std::uint8_t)0);
            }
        }
    }

    void instantiate(double sampleRate, int blockSize)
    {
        scratchBuf.resize((size_t)juce::jmax(8192, blockSize * 2), 0.0f);
        handle = lilv_plugin_instantiate(lilvPlugin, sampleRate, fmt->features);
        if (handle == nullptr)
            return;

        // Connect control ports to our buffer
        const std::uint32_t numPorts = lilv_plugin_get_num_ports(lilvPlugin);
        for (std::uint32_t i = 0; i < numPorts; ++i)
        {
            const LilvPort* port = lilv_plugin_get_port_by_index(lilvPlugin, i);
            if (isControlPort(port))
                lilv_instance_connect_port(handle, i, &controlValues[i]);
        }
        lilv_instance_activate(handle);
    }

    bool isAudioPort(const LilvPort* port) const
    {
        LilvNode* audioClass = lilv_new_uri(lilvWorld, LV2_AUDIO_PORT_URI);
        bool result = lilv_port_is_a(lilvPlugin, port, audioClass);
        lilv_node_free(audioClass);
        return result;
    }

    bool isControlPort(const LilvPort* port) const
    {
        LilvNode* ctrlClass = lilv_new_uri(lilvWorld, LV2_CONTROL_PORT_URI);
        bool result = lilv_port_is_a(lilvPlugin, port, ctrlClass);
        lilv_node_free(ctrlClass);
        return result;
    }

    bool isAtomPort(const LilvPort* port) const
    {
        LilvNode* atomClass = lilv_new_uri(lilvWorld, LV2_ATOM_PORT_URI);
        bool result = lilv_port_is_a(lilvPlugin, port, atomClass);
        lilv_node_free(atomClass);
        return result;
    }

    bool isInputPort(const LilvPort* port) const
    {
        LilvNode* inClass = lilv_new_uri(lilvWorld, LV2_INPUT_PORT_URI);
        bool result = lilv_port_is_a(lilvPlugin, port, inClass);
        lilv_node_free(inClass);
        return result;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Lv2PluginInstance)
};

// ---------------------------------------------------------------------------
// Lv2PluginFormat
// ---------------------------------------------------------------------------
Lv2PluginFormat::Lv2PluginFormat()
    : pimpl(std::make_unique<Pimpl>())
{
}

Lv2PluginFormat::~Lv2PluginFormat() = default;

bool Lv2PluginFormat::fileMightContainThisPluginType(const juce::String& fileOrIdentifier)
{
    // LV2 bundles are directories ending in .lv2; identifiers can also be URIs
    return juce::File(fileOrIdentifier).isDirectory()
        || fileOrIdentifier.startsWith("http://")
        || fileOrIdentifier.startsWith("https://");
}

juce::String Lv2PluginFormat::getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier)
{
    return fileOrIdentifier;
}

juce::FileSearchPath Lv2PluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath paths;
#if JUCE_LINUX
    paths.add(juce::File("/usr/lib/lv2"));
    paths.add(juce::File("/usr/local/lib/lv2"));
    paths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".lv2"));
#elif JUCE_MAC
    paths.add(juce::File("/Library/Audio/Plug-Ins/LV2"));
#elif JUCE_WINDOWS
    paths.add(juce::File("C:\\Program Files\\Common Files\\LV2"));
#endif
    return paths;
}

bool Lv2PluginFormat::pluginNeedsRescanning(const juce::PluginDescription&)
{
    return false;
}

bool Lv2PluginFormat::doesPluginStillExist(const juce::PluginDescription& desc)
{
    LilvNode* uri = lilv_new_uri(pimpl->world, desc.fileOrIdentifier.toRawUTF8());
    const LilvPlugins* plugins = lilv_world_get_all_plugins(pimpl->world);
    const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, uri);
    lilv_node_free(uri);
    return plugin != nullptr;
}

juce::StringArray Lv2PluginFormat::searchPathsForPlugins(
    const juce::FileSearchPath& dirs,
    bool recursive,
    bool)
{
    juce::StringArray results;
    for (int i = 0; i < dirs.getNumPaths(); ++i)
    {
        auto bundles = dirs[i].findChildFiles(
            juce::File::findDirectories, recursive, "*.lv2");
        for (auto& b : bundles)
            results.add(b.getFullPathName());
    }
    return results;
}

void Lv2PluginFormat::findAllTypesForFile(
    juce::OwnedArray<juce::PluginDescription>& results,
    const juce::String& fileOrIdentifier)
{
    // A plugin URI, e.g. when re-resolving a PluginDescription loaded from a saved
    // chain/preset.
    if (fileOrIdentifier.startsWith("http://") || fileOrIdentifier.startsWith("https://"))
    {
        LilvNode* uri = lilv_new_uri(pimpl->world, fileOrIdentifier.toRawUTF8());
        const LilvPlugins* all = lilv_world_get_all_plugins(pimpl->world);
        const LilvPlugin* p = lilv_plugins_get_by_uri(all, uri);
        lilv_node_free(uri);

        if (p != nullptr)
            results.add(createDescriptionForPlugin(p));
        return;
    }

    // The usual case: searchPathsForPlugins() hands us a bundle *directory* path
    // (e.g. "/usr/lib/lv2/calf.lv2"), not a URI. Find every plugin lilv knows about
    // whose bundle matches this directory — a bundle commonly holds many plugins.
    juce::String path = juce::File(fileOrIdentifier).getFullPathName();
    if (!path.endsWithChar('/'))
        path += "/";

    LilvNode* bundleUri = lilv_new_file_uri(pimpl->world, nullptr, path.toRawUTF8());
    if (bundleUri == nullptr)
        return;

    const LilvPlugins* plugins = lilv_world_get_all_plugins(pimpl->world);
    LILV_FOREACH(plugins, it, plugins)
    {
        const LilvPlugin* p = lilv_plugins_get(plugins, it);
        const LilvNode* pluginBundle = lilv_plugin_get_bundle_uri(p);
        if (pluginBundle != nullptr && lilv_node_equals(pluginBundle, bundleUri))
            results.add(createDescriptionForPlugin(p));
    }
    lilv_node_free(bundleUri);
}

void Lv2PluginFormat::createPluginInstance(
    const juce::PluginDescription& desc,
    double initialSampleRate,
    int initialBufferSize,
    PluginCreationCallback callback)
{
    LilvNode* uri = lilv_new_uri(pimpl->world, desc.fileOrIdentifier.toRawUTF8());
    const LilvPlugins* all = lilv_world_get_all_plugins(pimpl->world);
    const LilvPlugin* p = lilv_plugins_get_by_uri(all, uri);
    lilv_node_free(uri);

    if (p == nullptr)
    {
        callback(nullptr, "LV2 plugin URI not found: " + desc.fileOrIdentifier);
        return;
    }

    auto instance = std::make_unique<Lv2PluginInstance>(
        pimpl.get(), p, desc, initialSampleRate, initialBufferSize);

    callback(std::move(instance), {});
}

#endif // SOUNDPLAYER_LV2_SUPPORT
