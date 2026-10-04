#pragma once

#include <juce_core/juce_core.h>

// Turns a list of filesystem paths -- typically a drag-and-drop payload, which
// may mix files and folders -- into the set of files the player can actually
// open. Folders are expanded recursively; anything the audio format registry
// does not claim is dropped.
//
// "Supported" is defined by FormatManager (the same predicate the playback
// engine relies on), so a dropped file can never be offered for playback
// unless the engine has a decoder for it.
class AudioFileCollector
{
public:
    // True if the file has an extension the player can open. Directories are
    // not "supported" in this sense -- see collect().
    static bool isSupported(const juce::File& file);

    // Expands files and folders into playable files, in the order given.
    // Non-existent paths and unsupported files are skipped; a folder with no
    // supported children contributes nothing.
    static juce::Array<juce::File> collect(const juce::StringArray& paths);

private:
    AudioFileCollector() = delete;
};
