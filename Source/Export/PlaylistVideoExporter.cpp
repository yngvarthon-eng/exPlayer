#include "PlaylistVideoExporter.h"
#include "FfmpegProcess.h"
#include <algorithm>
#include <cstdio>

namespace
{
    using namespace FfmpegProcess;

    // Writes an ffmpeg concat-demuxer list file naming each of `files` in order.
    bool writeConcatList(const juce::File& listFile, const std::vector<juce::File>& files)
    {
        juce::String text;
        for (auto& f : files)
            text << "file " << shellQuote(f.getFullPathName()) << "\n";
        return listFile.replaceWithText(text);
    }

    juce::String readFfmpegLog(const juce::File& logFile)
    {
        auto log = logFile.loadFileAsString().trim();
        return log.isEmpty() ? juce::String() : (" ffmpeg said: " + log);
    }
}

void PlaylistVideoExporter::selectTracks(const juce::Array<PlaylistItem>& items, VideoExporter::ExportMode mode,
                                         std::vector<TrackSpec>& outTracks, std::vector<SkippedTrack>& outSkipped)
{
    outTracks.clear();
    outSkipped.clear();

    const bool needsPatternData = mode != VideoExporter::ExportMode::Visualization;

    for (auto& item : items)
    {
        if (item.isStream)
        {
            outSkipped.push_back({ item.name, "network stream" });
            continue;
        }
        if (item.isVideo)
        {
            outSkipped.push_back({ item.name, "video file, not an audio/tracker track" });
            continue;
        }
        if (needsPatternData && !(item.isModule || item.isXtpSequence))
        {
            outSkipped.push_back({ item.name, "no pattern data for this mode" });
            continue;
        }

        TrackSpec spec;
        spec.file = item.file;
        spec.isXtpSequence = item.isXtpSequence;
        spec.name = item.name;
        spec.estimatedDuration = item.duration;
        outTracks.push_back(std::move(spec));
    }
}

PlaylistVideoExporter::PlaylistVideoExporter(std::vector<TrackSpec> tracksIn, VideoExporter::Options optionsTemplateIn,
                                             Settings settingsIn, juce::File outputFileIn)
    : juce::Thread("playlist-video-exporter"),
      tracks(std::move(tracksIn)),
      optionsTemplate(std::move(optionsTemplateIn)),
      settings(settingsIn),
      outputFile(std::move(outputFileIn))
{
}

PlaylistVideoExporter::~PlaylistVideoExporter()
{
    stopThread(5000);
}

void PlaylistVideoExporter::finishWith(VideoExporter::Result r, const juce::String& message)
{
    result = r;
    resultMessage = message;
    finished.store(true);
}

juce::String PlaylistVideoExporter::getCurrentTrackLabel() const
{
    if (inStitchPhase.load())
        return "Combining tracks...";

    juce::String name;
    {
        const juce::ScopedLock sl(labelLock);
        name = currentTrackNameForLabel;
    }
    return "Rendering track " + juce::String(currentTrackIndex.load() + 1) + " of "
         + juce::String((int) tracks.size()) + ": " + name;
}

bool PlaylistVideoExporter::renderTrack(const TrackSpec& track, const juce::File& outFile,
                                        double elapsedWeight, double totalWeight, double trackWeight,
                                        double& outRenderedDuration)
{
    {
        const juce::ScopedLock sl(labelLock);
        currentTrackNameForLabel = track.name;
    }

    VideoExporter::Options opts = optionsTemplate;
    opts.sourceFile = track.file;
    opts.isXtpSequence = track.isXtpSequence;
    opts.outputFile = outFile;
    if (settings.showTrackTitles)
    {
        opts.trackTitleOverlay.text = track.name;
        opts.trackTitleOverlay.slide = VideoExporter::TextSlide::TimedSlideInOut;
        opts.trackTitleOverlay.holdSeconds = 2.5;
    }

    VideoExporter exporter(std::move(opts));
    exporter.startExport();

    while (!exporter.hasFinished())
    {
        if (threadShouldExit())
            exporter.cancel();

        if (totalWeight > 0.0)
            progress.store(juce::jlimit(0.0, 1.0,
                (elapsedWeight + exporter.getProgress() * trackWeight) / totalWeight));

        juce::Thread::sleep(30);
    }
    exporter.waitForThreadToExit(5000);

    const auto trackResult = exporter.getResult();
    if (trackResult == VideoExporter::Result::Cancelled || threadShouldExit())
    {
        finishWith(VideoExporter::Result::Cancelled, "Export cancelled.");
        return false;
    }
    if (trackResult != VideoExporter::Result::Success)
    {
        finishWith(VideoExporter::Result::Failed,
                  "Failed to render \"" + track.name + "\": " + exporter.getResultMessage());
        return false;
    }

    outRenderedDuration = exporter.getRenderedDurationSeconds();
    return true;
}

void PlaylistVideoExporter::run()
{
    if (tracks.empty())
    {
        finishWith(VideoExporter::Result::Failed, "No exportable tracks.");
        return;
    }

    const auto outputParent = outputFile.getParentDirectory();
    if (!outputParent.isDirectory())
    {
        finishWith(VideoExporter::Result::Failed, "Output folder does not exist: " + outputParent.getFullPathName());
        return;
    }
    if (!outputParent.hasWriteAccess())
    {
        finishWith(VideoExporter::Result::Failed, "No permission to write to: " + outputParent.getFullPathName());
        return;
    }

    // Weight each track's share of the progress bar by its known duration; if any track's
    // duration is unknown (0 -- not every format has one available before playback), fall
    // back to weighting every track equally rather than mixing known and unknown weights.
    const bool haveAllDurations = std::all_of(tracks.begin(), tracks.end(),
        [](const TrackSpec& t) { return t.estimatedDuration > 0.0; });

    std::vector<double> weights(tracks.size(), 1.0);
    double totalWeight = (double) tracks.size();
    if (haveAllDurations)
    {
        totalWeight = 0.0;
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            weights[i] = tracks[i].estimatedDuration;
            totalWeight += weights[i];
        }
    }

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto uid = juce::String::toHexString(juce::Random::getSystemRandom().nextInt64());

    std::vector<juce::File> trackFiles;
    std::vector<double> renderedDurations;
    auto cleanupTrackFiles = [&] { for (auto& f : trackFiles) f.deleteFile(); };

    double elapsed = 0.0;
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        currentTrackIndex.store((int) i);
        inStitchPhase.store(false);

        auto trackFile = tempDir.getChildFile("sp2playlist_" + uid + "_track" + juce::String((int) i) + ".mp4");
        double renderedDuration = 0.0;
        if (!renderTrack(tracks[i], trackFile, elapsed, totalWeight, weights[i], renderedDuration))
        {
            cleanupTrackFiles();
            return; // renderTrack already called finishWith
        }

        trackFiles.push_back(trackFile);
        renderedDurations.push_back(renderedDuration);
        elapsed += weights[i];
    }

    inStitchPhase.store(true);
    progress.store(0.97);

    juce::String error;
    bool stitched = false;
    switch (settings.transition)
    {
        case TransitionMode::HardCut:   stitched = stitchHardCut(trackFiles, outputFile, error); break;
        case TransitionMode::Silence:   stitched = stitchSilence(trackFiles, outputFile, error); break;
        case TransitionMode::Crossfade: stitched = stitchCrossfade(trackFiles, renderedDurations, outputFile, error); break;
    }

    cleanupTrackFiles();

    if (!stitched)
    {
        finishWith(VideoExporter::Result::Failed, "Failed to combine tracks." + error);
        return;
    }

    finishWith(VideoExporter::Result::Success, "Video exported to " + outputFile.getFullPathName());
}

bool PlaylistVideoExporter::stitchHardCut(const std::vector<juce::File>& trackFiles, const juce::File& out,
                                          juce::String& error)
{
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto uid = juce::String::toHexString(juce::Random::getSystemRandom().nextInt64());
    auto listFile = tempDir.getChildFile("sp2playlist_" + uid + "_concat.txt");
    auto logFile = tempDir.getChildFile("sp2playlist_" + uid + "_ffmpeg.log");
    auto cleanup = [&] { listFile.deleteFile(); logFile.deleteFile(); };

    if (!writeConcatList(listFile, trackFiles))
    {
        error = "Could not write the concat list file.";
        cleanup();
        return false;
    }

    // Re-encoded rather than -c copy: per-track audio sample rates can differ (an .xtp
    // song's fixed render rate vs. a plain audio file's native rate), which breaks
    // stream-copy concat across mismatched AAC parameters.
    const juce::String cmd = "ffmpeg -y -hide_banner -loglevel error -f concat -safe 0 -i "
        + shellQuote(listFile.getFullPathName())
        + " -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -movflags +faststart "
        + shellQuote(out.getFullPathName()) + " 2>" + shellQuote(logFile.getFullPathName());

    FILE* pipe = openPipe(cmd, "r");
    if (pipe == nullptr)
    {
        error = "Could not start ffmpeg for concatenation.";
        cleanup();
        return false;
    }
    char discard[256];
    while (fgets(discard, sizeof(discard), pipe) != nullptr) {}
    const bool ok = exitedCleanly(closePipe(pipe));

    if (!ok)
        error = readFfmpegLog(logFile);
    cleanup();
    return ok;
}

bool PlaylistVideoExporter::stitchSilence(const std::vector<juce::File>& trackFiles, const juce::File& out,
                                          juce::String& error)
{
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto uid = juce::String::toHexString(juce::Random::getSystemRandom().nextInt64());
    auto fillerFile = tempDir.getChildFile("sp2playlist_" + uid + "_filler.mp4");
    auto listFile = tempDir.getChildFile("sp2playlist_" + uid + "_concat.txt");
    auto logFile = tempDir.getChildFile("sp2playlist_" + uid + "_ffmpeg.log");
    auto cleanup = [&] { fillerFile.deleteFile(); listFile.deleteFile(); logFile.deleteFile(); };

    const juce::String size = juce::String(optionsTemplate.width) + "x" + juce::String(optionsTemplate.height);
    const juce::String fillerCmd = "ffmpeg -y -hide_banner -loglevel error -f lavfi -i "
        + shellQuote("color=c=black:s=" + size + ":r=" + juce::String(VideoExporter::kFps))
        + " -f lavfi -i " + shellQuote("anullsrc=r=44100:cl=stereo")
        + " -t " + juce::String(settings.transitionSeconds, 2)
        + " -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac "
        + shellQuote(fillerFile.getFullPathName()) + " 2>" + shellQuote(logFile.getFullPathName());

    FILE* fillerPipe = openPipe(fillerCmd, "r");
    if (fillerPipe == nullptr || !exitedCleanly(closePipe(fillerPipe)))
    {
        error = "Could not generate the silence filler clip." + readFfmpegLog(logFile);
        cleanup();
        return false;
    }

    std::vector<juce::File> withFiller;
    for (size_t i = 0; i < trackFiles.size(); ++i)
    {
        withFiller.push_back(trackFiles[i]);
        if (i + 1 < trackFiles.size())
            withFiller.push_back(fillerFile);
    }

    if (!writeConcatList(listFile, withFiller))
    {
        error = "Could not write the concat list file.";
        cleanup();
        return false;
    }

    const juce::String cmd = "ffmpeg -y -hide_banner -loglevel error -f concat -safe 0 -i "
        + shellQuote(listFile.getFullPathName())
        + " -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -movflags +faststart "
        + shellQuote(out.getFullPathName()) + " 2>" + shellQuote(logFile.getFullPathName());

    FILE* pipe = openPipe(cmd, "r");
    if (pipe == nullptr)
    {
        error = "Could not start ffmpeg for concatenation.";
        cleanup();
        return false;
    }
    char discard[256];
    while (fgets(discard, sizeof(discard), pipe) != nullptr) {}
    const bool ok = exitedCleanly(closePipe(pipe));

    if (!ok)
        error = readFfmpegLog(logFile);
    cleanup();
    return ok;
}

bool PlaylistVideoExporter::stitchCrossfade(const std::vector<juce::File>& trackFiles,
                                            const std::vector<double>& durations,
                                            const juce::File& out, juce::String& error)
{
    if (trackFiles.size() == 1)
        return stitchHardCut(trackFiles, out, error); // nothing to cross-fade

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto uid = juce::String::toHexString(juce::Random::getSystemRandom().nextInt64());
    auto logFile = tempDir.getChildFile("sp2playlist_" + uid + "_ffmpeg.log");
    auto cleanup = [&] { logFile.deleteFile(); };

    const double xfade = settings.transitionSeconds;

    juce::String inputs;
    for (auto& f : trackFiles)
        inputs << "-i " << shellQuote(f.getFullPathName()) << " ";

    // Chains xfade (video) / acrossfade (audio) pairwise across all tracks. Each xfade's
    // `offset` is where in the running merged timeline the next clip starts overlapping,
    // i.e. the summed duration of everything already merged minus the overlaps already
    // consumed by prior crossfades.
    juce::String videoChain, audioChain;
    juce::String prevV = "0:v", prevA = "0:a";
    double runningDuration = durations[0];

    for (size_t i = 1; i < trackFiles.size(); ++i)
    {
        const juce::String curV = juce::String(i) + ":v";
        const juce::String curA = juce::String(i) + ":a";
        const bool isLast = (i + 1 == trackFiles.size());
        const juce::String outV = isLast ? "vout" : ("v" + juce::String((int) i));
        const juce::String outA = isLast ? "aout" : ("a" + juce::String((int) i));

        const double offset = runningDuration - xfade;
        videoChain << "[" << prevV << "][" << curV << "]xfade=transition=fade:duration="
                  << juce::String(xfade, 2) << ":offset=" << juce::String(juce::jmax(0.0, offset), 2)
                  << "[" << outV << "];";
        audioChain << "[" << prevA << "][" << curA << "]acrossfade=d=" << juce::String(xfade, 2)
                  << "[" << outA << "];";

        runningDuration += durations[i] - xfade;
        prevV = outV;
        prevA = outA;
    }

    juce::String filterComplex = videoChain + audioChain;
    filterComplex = filterComplex.dropLastCharacters(1); // trailing ';'

    const juce::String cmd = "ffmpeg -y -hide_banner -loglevel error " + inputs
        + "-filter_complex " + shellQuote(filterComplex)
        + " -map \"[vout]\" -map \"[aout]\" -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -movflags +faststart "
        + shellQuote(out.getFullPathName()) + " 2>" + shellQuote(logFile.getFullPathName());

    FILE* pipe = openPipe(cmd, "r");
    if (pipe == nullptr)
    {
        error = "Could not start ffmpeg for crossfade stitching.";
        cleanup();
        return false;
    }
    char discard[256];
    while (fgets(discard, sizeof(discard), pipe) != nullptr) {}
    const bool ok = exitedCleanly(closePipe(pipe));

    if (!ok)
        error = readFfmpegLog(logFile);
    cleanup();
    return ok;
}
