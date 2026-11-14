#include "MidiInputSettings.h"
#include "../Util/AppDataDirectory.h"

juce::File MidiInputSettings::getSettingsFile()
{
    return getAppDataDirectory()
               .getChildFile("midi_inputs.xml");
}

juce::StringArray MidiInputSettings::loadEnabledIdentifiers()
{
    juce::StringArray result;

    auto file = getSettingsFile();
    if (!file.existsAsFile())
        return result;

    auto xml = juce::XmlDocument::parse(file);
    if (!xml || xml->getTagName() != "MidiInputs")
        return result;

    for (auto* child : xml->getChildIterator())
        if (child->getTagName() == "Device")
            result.add(child->getStringAttribute("identifier"));

    return result;
}

void MidiInputSettings::saveEnabledIdentifiers(const juce::StringArray& identifiers)
{
    auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();

    juce::XmlElement root("MidiInputs");
    for (const auto& identifier : identifiers)
        root.createNewChildElement("Device")->setAttribute("identifier", identifier);

    root.writeTo(file);
}
