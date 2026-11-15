#include "DriveAwareFileBrowser.h"

namespace
{
   #if JUCE_LINUX || JUCE_BSD
    // lsblk -P escapes unsafe characters in values as \xNN.
    juce::String unescapeLsblk(const juce::String& s)
    {
        juce::MemoryOutputStream out;
        auto utf8 = s.toStdString();
        for (size_t i = 0; i < utf8.size(); ++i)
        {
            if (utf8[i] == '\\' && i + 3 < utf8.size() && utf8[i + 1] == 'x')
            {
                out.writeByte((char) juce::String(utf8.substr(i + 2, 2)).getHexValue32());
                i += 3;
            }
            else
                out.writeByte(utf8[i]);
        }
        return out.toUTF8();
    }

    // Parses one `KEY="value" KEY="value"` line of lsblk -P output.
    juce::StringPairArray parseLsblkLine(const juce::String& line)
    {
        juce::StringPairArray values;
        auto rest = line.trim();
        while (rest.isNotEmpty())
        {
            const auto eq = rest.indexOf("=\"");
            if (eq <= 0) break;
            const auto key = rest.substring(0, eq).trim();
            const auto close = rest.indexOfChar(eq + 2, '"');
            if (close < 0) break;
            values.set(key, unescapeLsblk(rest.substring(eq + 2, close)));
            rest = rest.substring(close + 1).trim();
        }
        return values;
    }

    juce::String runAndCapture(const juce::StringArray& command, int timeoutMs, int* exitCode = nullptr)
    {
        juce::ChildProcess process;
        if (! process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            if (exitCode != nullptr) *exitCode = -1;
            return {};
        }
        auto output = process.readAllProcessOutput();
        if (! process.waitForProcessToFinish(timeoutMs))
            process.kill();
        if (exitCode != nullptr) *exitCode = (int) process.getExitCode();
        return output;
    }
   #endif
}

DriveAwareFileBrowser::DriveAwareFileBrowser(int flags, const juce::File& initialFileOrDirectory,
                                             const juce::FileFilter* fileFilter, juce::FilePreviewComponent* previewComp)
    : juce::FileBrowserComponent(flags, initialFileOrDirectory, fileFilter, previewComp)
{
    // The base constructor fills the location box before this override of
    // getRoots() exists, so rebuild it now, then restore the current path text.
    refreshRoots();
    resetRecentPaths();
    setRoot(getRoot());
    lastRealRoot = getRoot();
    addListener(&rootWatcher);
}

DriveAwareFileBrowser::~DriveAwareFileBrowser()
{
    removeListener(&rootWatcher);
}

void DriveAwareFileBrowser::getRoots(juce::StringArray& rootNames, juce::StringArray& rootPaths)
{
    rootNames = cachedNames;
    rootPaths = cachedPaths;
}

void DriveAwareFileBrowser::refreshRoots()
{
    juce::StringArray rootNames, rootPaths;
    getDefaultRoots(rootNames, rootPaths);

   #if JUCE_LINUX || JUCE_BSD
    const auto user = juce::SystemStats::getLogonName();
    const juce::File mountParents[] { juce::File("/run/media/" + user),
                                      juce::File("/media/" + user),
                                      juce::File("/mnt") };

    bool addedSeparator = false;
    auto addSeparatorOnce = [&]
    {
        if (addedSeparator) return;
        rootNames.add({});
        rootPaths.add({});
        addedSeparator = true;
    };

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

            addSeparatorOnce();
            rootNames.add(volume.getFileName());
            rootPaths.add(path);
        }
    }

    // Partitions not mounted anywhere yet: listed by device, mounted on pick
    // (see rootChanged()).
    unmountedDrives = findUnmountedDrives();
    for (const auto& drive : unmountedDrives)
    {
        addSeparatorOnce();
        const auto deviceName = juce::File(drive.device).getFileName();
        auto name = drive.label.isNotEmpty() ? drive.label + " (" + deviceName + ")" : deviceName;
        rootNames.add(name + ", " + drive.size + " - not mounted, select to mount");
        rootPaths.add(drive.device);
    }
   #endif

    cachedNames = rootNames;
    cachedPaths = rootPaths;
}

juce::Array<DriveAwareFileBrowser::UnmountedDrive> DriveAwareFileBrowser::findUnmountedDrives()
{
    juce::Array<UnmountedDrive> drives;

   #if JUCE_LINUX || JUCE_BSD
    const auto output = runAndCapture({ "lsblk", "-P", "-o", "PATH,FSTYPE,LABEL,SIZE,MOUNTPOINT,TYPE" }, 2000);

    // Filesystems udisks can mount and that can hold music; swap, LVM/RAID
    // members, encrypted containers and the like are left out.
    static const juce::StringArray mountable { "ext2", "ext3", "ext4", "xfs", "btrfs", "f2fs", "vfat",
                                               "exfat", "ntfs", "ntfs3", "hfsplus", "iso9660", "udf" };

    for (const auto& line : juce::StringArray::fromLines(output))
    {
        const auto v = parseLsblkLine(line);
        const auto type = v["TYPE"];
        if (v["MOUNTPOINT"].isNotEmpty() || ! mountable.contains(v["FSTYPE"])
            || ! (type == "part" || type == "disk" || type == "rom"))
            continue;

        drives.add({ v["PATH"], v["LABEL"], v["SIZE"] });
    }
   #endif

    return drives;
}

juce::String DriveAwareFileBrowser::mountPointOf(const juce::String& device)
{
   #if JUCE_LINUX || JUCE_BSD
    return runAndCapture({ "lsblk", "-n", "-o", "MOUNTPOINT", device }, 2000).trim().upToFirstOccurrenceOf("\n", false, false);
   #else
    juce::ignoreUnused(device);
    return {};
   #endif
}

void DriveAwareFileBrowser::rootChanged(const juce::File& newRoot)
{
    const auto path = newRoot.getFullPathName();
    for (const auto& drive : unmountedDrives)
    {
        if (drive.device == path)
        {
            // Don't sit on the device node (an empty, meaningless listing):
            // go back where we were while it mounts. Deferred, since this is
            // called from inside setRoot().
            juce::Component::SafePointer<DriveAwareFileBrowser> safeThis(this);
            const auto back = lastRealRoot;
            juce::MessageManager::callAsync([safeThis, back]
            {
                if (safeThis != nullptr) safeThis->setRoot(back);
            });
            mountAndOpen(drive);
            return;
        }
    }

    if (newRoot.isDirectory())
        lastRealRoot = newRoot;
}

void DriveAwareFileBrowser::mountAndOpen(const UnmountedDrive& drive)
{
   #if JUCE_LINUX || JUCE_BSD
    if (mounting) return;
    mounting = true;

    // udisksctl can block on the system's password prompt, so it runs off the
    // message thread; the result comes back to it.
    juce::Component::SafePointer<DriveAwareFileBrowser> safeThis(this);
    const auto device = drive.device;
    juce::Thread::launch([safeThis, device]
    {
        int exitCode = -1;
        auto output = runAndCapture({ "udisksctl", "mount", "-b", device }, 120000, &exitCode);
        if (exitCode == -1 && output.isEmpty())
            output = "Couldn't run udisksctl. Is udisks2 installed?";

        juce::MessageManager::callAsync([safeThis, device, ok = exitCode == 0, output]
        {
            if (safeThis != nullptr)
                safeThis->mountFinished(device, ok, output.trim());
        });
    });
   #else
    juce::ignoreUnused(drive);
   #endif
}

void DriveAwareFileBrowser::mountFinished(const juce::String& device, bool succeeded, const juce::String& message)
{
    mounting = false;

    const auto mountPoint = succeeded ? mountPointOf(device) : juce::String();
    if (! succeeded || mountPoint.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Couldn't mount " + device,
                                               message.isNotEmpty() ? message : "The drive didn't report a mount point.");
        return;
    }

    // The drive moves from "not mounted" to the mounted volumes.
    refreshRoots();
    resetRecentPaths();
    setRoot(juce::File(mountPoint));
}
