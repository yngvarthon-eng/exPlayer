#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// JUCE's built-in file browser only offers "/", Home and Desktop as roots on
// Linux, so drives mounted elsewhere are awkward to reach. This adds every
// mounted volume found under the usual removable/secondary mount points --
// and, on Linux, also the partitions that aren't mounted yet (a desktop only
// mounts internal data disks once they're opened in the file manager, so
// after a reboot they'd otherwise be missing). Picking one of those mounts it
// through udisks, the same way the file manager does (which may show the
// system's password prompt), and then opens it.
class DriveAwareFileBrowser : public juce::FileBrowserComponent
{
public:
    DriveAwareFileBrowser(int flags, const juce::File& initialFileOrDirectory,
                          const juce::FileFilter* fileFilter, juce::FilePreviewComponent* previewComp);
    ~DriveAwareFileBrowser() override;

    void getRoots(juce::StringArray& rootNames, juce::StringArray& rootPaths) override;

private:
    struct UnmountedDrive
    {
        juce::String device; // "/dev/sda1" -- also used as its entry's root path
        juce::String label;  // filesystem label, may be empty
        juce::String size;   // as lsblk prints it, e.g. "931.5G"
    };

    // Rebuilds the cached root list. getRoots() is called several times per
    // selection and the base class matches combo-box indices against it, so it
    // must not change between calls -- only when this is called.
    void refreshRoots();

    static juce::Array<UnmountedDrive> findUnmountedDrives();
    static juce::String mountPointOf(const juce::String& device);

    void rootChanged(const juce::File& newRoot);

    // FileBrowserComponent is itself a (private) FileBrowserListener, so the
    // root-change notification is taken through a separate listener object.
    struct RootWatcher : juce::FileBrowserListener
    {
        explicit RootWatcher(DriveAwareFileBrowser& o) : owner(o) {}
        void browserRootChanged(const juce::File& newRoot) override { owner.rootChanged(newRoot); }
        void selectionChanged() override {}
        void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
        void fileDoubleClicked(const juce::File&) override {}
        DriveAwareFileBrowser& owner;
    };
    RootWatcher rootWatcher { *this };

    void mountAndOpen(const UnmountedDrive& drive);
    void mountFinished(const juce::String& device, bool succeeded, const juce::String& message);

    juce::StringArray cachedNames, cachedPaths;
    juce::Array<UnmountedDrive> unmountedDrives;
    juce::File lastRealRoot;
    bool mounting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriveAwareFileBrowser)
};
