#pragma once

#include <juce_core/juce_core.h>

// The folder where exPlayer keeps its settings, presets and logs:
// <userApplicationDataDirectory>/exPlayer (~/.config/exPlayer on Linux).
//
// exPlayer used to be called SoundPlayer2 and kept these files in
// ~/.config/SoundPlayer2. The first time the folder is asked for, and only if
// the new folder doesn't exist yet, the old folder is copied over so settings
// carry across the rename. The old folder is left untouched.
juce::File getAppDataDirectory();
