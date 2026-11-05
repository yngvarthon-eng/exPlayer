#include "AppLogger.h"

#include <iostream>

AppLogger::AppLogger()
    : logFile(getDefaultLogFile())
{
    logFile.getParentDirectory().createDirectory();
    juce::FileLogger::trimFileSize(logFile, MAX_FILE_SIZE_BYTES);
}

juce::File AppLogger::getDefaultLogFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("SoundPlayer2")
               .getChildFile("logs")
               .getChildFile("soundplayer2.log");
}

void AppLogger::logMessage(const juce::String& message)
{
    const auto line = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S ") + message;

    // Messages can arrive from the audio, export and message threads at once;
    // the lock keeps lines from interleaving in either destination.
    const juce::ScopedLock sl(logLock);

    std::cerr << line << std::endl;

    // Opened per message, as juce::FileLogger does, so the file stays readable
    // (and deletable) while the app runs and a crash loses at most one line.
    juce::FileOutputStream out(logFile, 256);
    if (out.openedOk())
        out << line << juce::newLine;
}
