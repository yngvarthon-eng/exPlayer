#include "AudioFileCollector.h"
#include "../Audio/FormatManager.h"

namespace
{
    // One registry for the whole process -- registering formats has no side
    // effects, but there is no reason to rebuild it for every dropped file.
    // isSupportedFormat() only inspects the extension.
    const FormatManager& formatRegistry()
    {
        static const FormatManager registry;
        return registry;
    }
}

bool AudioFileCollector::isSupported(const juce::File& file)
{
    return formatRegistry().isSupportedFormat(file);
}

juce::Array<juce::File> AudioFileCollector::collect(const juce::StringArray& paths)
{
    juce::Array<juce::File> collected;

    for (const auto& path : paths)
    {
        const juce::File file(path);

        if (file.isDirectory())
        {
            for (const auto& child : file.findChildFiles(juce::File::findFiles, true))
                if (isSupported(child))
                    collected.add(child);
        }
        else if (file.existsAsFile() && isSupported(file))
        {
            collected.add(file);
        }
    }

    return collected;
}
