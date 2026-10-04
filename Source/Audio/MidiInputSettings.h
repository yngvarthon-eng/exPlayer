#pragma once

#include <juce_core/juce_core.h>

/**
 * Persists which hardware MIDI input device identifiers the user has enabled to
 * feed the FX chain. Nothing is enabled by default — a device (e.g. a keyboard
 * that shows up under more than one driver/port) only feeds the chain once picked,
 * so a controller that enumerates more than once never silently double-delivers.
 *
 * Mirrors EqPresetManager/PluginChainPersistence's file convention.
 */
namespace MidiInputSettings
{
    juce::File getSettingsFile();

    juce::StringArray loadEnabledIdentifiers();
    void saveEnabledIdentifiers(const juce::StringArray& identifiers);
}
