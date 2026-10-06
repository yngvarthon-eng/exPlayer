#include "DriveAwareFileBrowser.h"

void DriveAwareFileBrowser::getRoots(juce::StringArray& rootNames, juce::StringArray& rootPaths)
{
    getDefaultRoots(rootNames, rootPaths);

   #if JUCE_LINUX || JUCE_BSD
    const auto user = juce::SystemStats::getLogonName();
    const juce::File mountParents[] { juce::File("/run/media/" + user),
                                      juce::File("/media/" + user),
                                      juce::File("/mnt") };

    bool addedSeparator = false;

    for (const auto& parent : mountParents)
    {
        if (! parent.isDirectory())
            continue;

        auto volumes = parent.findChildFiles(juce::File::findDirectories, false);
        volumes.sort();

        for (const auto& volume : volumes)
        {
            const auto path = volume.getFullPathName();
            if (volume.getFileName().startsWithChar('.') || rootPaths.contains(path))
                continue;

            if (! addedSeparator)
            {
                rootNames.add({});
                rootPaths.add({});
                addedSeparator = true;
            }

            rootNames.add(volume.getFileName());
            rootPaths.add(path);
        }
    }
   #endif
}
