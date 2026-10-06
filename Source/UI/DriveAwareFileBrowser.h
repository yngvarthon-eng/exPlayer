#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// JUCE's built-in file browser only offers "/", Home and Desktop as roots on
// Linux, so drives mounted elsewhere are awkward to reach. This adds every
// mounted volume found under the usual removable/secondary mount points.
class DriveAwareFileBrowser : public juce::FileBrowserComponent
{
public:
    DriveAwareFileBrowser(int flags, const juce::File& initialFileOrDirectory,
                          const juce::FileFilter* fileFilter, juce::FilePreviewComponent* previewComp);

    void getRoots(juce::StringArray& rootNames, juce::StringArray& rootPaths) override;
};
