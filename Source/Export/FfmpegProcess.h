#pragma once

#include <juce_core/juce_core.h>
#include <cstdio>

#if defined(_WIN32)
#include <stdio.h>
#else
#include <sys/wait.h>
#endif

// Small process-pipe helpers shared by everything that drives ffmpeg as a
// subprocess (VideoExporter's offline renders, ScreenRecorder's live capture).
namespace FfmpegProcess
{
    inline juce::String shellQuote(const juce::String& path)
    {
        return "'" + path.replace("'", "'\\''") + "'";
    }

#if defined(_WIN32)
    inline FILE* openPipe(const juce::String& cmd, const char* mode)  { return _popen(cmd.toRawUTF8(), mode); }
    inline int   closePipe(FILE* f)                                   { return _pclose(f); }
    inline bool  exitedCleanly(int status)                            { return status == 0; }
#else
    inline FILE* openPipe(const juce::String& cmd, const char* mode)  { return popen(cmd.toRawUTF8(), mode); }
    inline int   closePipe(FILE* f)                                   { return pclose(f); }
    inline bool  exitedCleanly(int status)                            { return WIFEXITED(status) && WEXITSTATUS(status) == 0; }
#endif

    // Kills a still-running ffmpeg juce::ChildProcess and blocks until it's
    // actually reaped -- kill() alone doesn't waitpid() it, so without this a
    // timed-out wait would abandon a zombie forever (nothing else ever
    // waitpid()s that pid again). SIGKILL is uncatchable and normally reaps
    // almost instantly; the retries only matter under genuinely heavy system
    // load (e.g. many rapid seeks queued back to back on a video decoder),
    // which is exactly when this must not silently give up.
    inline void killAndReap(juce::ChildProcess& process, const char* diagnosticLabel = "ffmpeg")
    {
        if (!process.isRunning())
            return;

        process.kill();

        bool reaped = false;
        for (int attempt = 0; attempt < 3 && !reaped; ++attempt)
            reaped = process.waitForProcessToFinish(2000);

        if (!reaped)
            fprintf(stderr, "%s: did not exit after SIGKILL + 6s -- leaking a zombie process\n", diagnosticLabel);
    }
}
