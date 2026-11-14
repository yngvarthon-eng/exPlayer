#include "AppDataDirectory.h"

juce::File getAppDataDirectory()
{
    static const juce::File dir = []
    {
        const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
        const auto current = base.getChildFile ("exPlayer");
        const auto legacy = base.getChildFile ("SoundPlayer2");

        if (! current.exists() && legacy.isDirectory())
            legacy.copyDirectoryTo (current);

        return current;
    }();

    return dir;
}
