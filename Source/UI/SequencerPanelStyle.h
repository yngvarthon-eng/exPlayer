#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// Shared look and helpers for the fullscreen sequencer's side-panel sections
// (SequencerSidePanel and the components it stacks), matching the pattern
// grid's palette in ModulePatternViewer.cpp.
namespace SequencerPanelStyle
{
    inline const juce::Colour panelBg    { 0xff0e0e1a };
    inline const juce::Colour headerBg   { 0xff0c0c18 };
    inline const juce::Colour headerText { 0xff8899aa };
    inline const juce::Colour separator  { 0xff252540 };
    inline const juce::Colour dimText    { 0xff6a6a8a };
    inline const juce::Colour playColour { 0xffff7a1a };

    constexpr int sectionHeaderH = 20;

    inline juce::Font headerFont()
    {
        return juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold);
    }

    // The strip along the top of each section with its caption; returns the
    // area below it.
    inline juce::Rectangle<int> drawSectionHeader(juce::Graphics& g, juce::Rectangle<int> bounds,
                                                  const juce::String& caption)
    {
        auto header = bounds.removeFromTop(sectionHeaderH);
        g.setColour(headerBg);
        g.fillRect(header);
        g.setColour(separator);
        g.drawHorizontalLine(header.getBottom() - 1, (float) header.getX(), (float) header.getRight());
        g.setFont(headerFont());
        g.setColour(headerText);
        g.drawText(caption, header.reduced(6, 0), juce::Justification::centredLeft);
        return bounds;
    }

    // One colour per instrument number (ModulePatternData::parseInstrumentNumber),
    // spread round the hue circle by the golden ratio so neighbouring numbers
    // differ clearly.
    inline juce::Colour instrumentColour(int instrument)
    {
        return juce::Colour::fromHSV(std::fmod(0.08f + (float) instrument * 0.618034f, 1.0f), 0.65f, 0.95f, 1.0f);
    }

    // "C-5" / "C#5" -> MIDI-style note number (C-5 = 60), or -1 for anything
    // that isn't a played note (empty, cut, key-off, fade).
    inline int parseNote(const juce::String& note)
    {
        if (note.length() < 3) return -1;
        static const int semitone[] = { 9, 11, 0, 2, 4, 5, 7 }; // A..G
        const auto letter = juce::CharacterFunctions::toUpperCase(note[0]);
        if (letter < 'A' || letter > 'G') return -1;
        const auto octaveChar = note[2];
        if (! juce::CharacterFunctions::isDigit(octaveChar)) return -1;
        int n = semitone[letter - 'A'] + (note[1] == '#' ? 1 : 0);
        return n + 12 * (octaveChar - '0');
    }

    inline bool isNoteStop(const juce::String& note)
    {
        return note == "^^^" || note == "===" || note == "~~~";
    }
}
