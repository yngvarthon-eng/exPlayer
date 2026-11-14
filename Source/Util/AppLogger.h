#pragma once

#include <juce_core/juce_core.h>

// Application-wide logger: every message goes to stderr and is appended to a
// log file, each line prefixed with a timestamp.
//
// Installed once in Main.cpp via juce::Logger::setCurrentLogger(), after which
// any juce::Logger::writeToLog() call in the app ends up in both places.
//
// The file lives at <userApplicationDataDirectory>/exPlayer/logs/explayer.log
// (~/.config/exPlayer/logs/explayer.log on Linux), next to the app's
// other settings files. It is trimmed to its newest MAX_FILE_SIZE_BYTES at
// startup so it never grows without bound.
class AppLogger : public juce::Logger
{
public:
    AppLogger();
    ~AppLogger() override = default;

    void logMessage(const juce::String& message) override;

    const juce::File& getLogFile() const noexcept { return logFile; }

    static juce::File getDefaultLogFile();

private:
    static constexpr juce::int64 MAX_FILE_SIZE_BYTES = 1024 * 1024;

    juce::File logFile;
    juce::CriticalSection logLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppLogger)
};
