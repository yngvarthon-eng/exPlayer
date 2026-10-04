#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;

// The hardware MIDI input picker: a checklist of connected MIDI inputs, popped
// up under whichever button asks for it.
//
// Nothing is enabled by default, because a controller that enumerates under
// more than one driver or port would otherwise have every note delivered twice.
// This menu is therefore the only way a device starts feeding the FX chain.
class MidiInputMenu
{
public:
    // Shows the checklist anchored to `target`. Reports with a message box
    // instead of an empty menu when no inputs are connected.
    static void show(AudioEngine& engine, juce::Component& target);

private:
    MidiInputMenu() = delete;
};
