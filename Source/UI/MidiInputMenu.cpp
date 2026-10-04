#include "MidiInputMenu.h"
#include "../Audio/AudioEngine.h"

void MidiInputMenu::show(AudioEngine& engine, juce::Component& target)
{
    const auto devices = engine.getAvailableMidiInputs();

    if (devices.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No MIDI inputs found",
            "No hardware MIDI input devices are currently connected.");
        return;
    }

    juce::PopupMenu menu;

    for (int i = 0; i < devices.size(); ++i)
        menu.addItem(i + 1, devices[i].name, true,
                     engine.isMidiInputEnabled(devices[i].identifier));

    // The device list is captured by value: the popup answers long after this
    // call returns, and the audio device manager may re-enumerate in between.
    auto* enginePtr = &engine;

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target),
        [enginePtr, devices](int result)
        {
            if (result <= 0 || ! juce::isPositiveAndBelow(result - 1, devices.size()))
                return;

            const auto& identifier = devices[result - 1].identifier;
            enginePtr->setMidiInputEnabled(identifier,
                                           ! enginePtr->isMidiInputEnabled(identifier));
        });
}
