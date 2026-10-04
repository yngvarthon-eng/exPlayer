#include "PlaylistVideoExportController.h"
#include "../Audio/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"
#include "../UI/VideoExportSettingsDialog.h"
#include "../UI/VisualizationComponent.h"

namespace
{
    // True as soon as one candidate track has pattern data, which is what makes
    // the pattern-grid layouts worth offering in the settings dialog.
    bool playlistHasPatternData(const juce::Array<PlaylistItem>& items)
    {
        for (const auto& item : items)
            if (! item.isStream && ! item.isVideo && (item.isModule || item.isXtpSequence))
                return true;

        return false;
    }

    // Some save-dialog paths on Linux (zenity, in particular) can hand back a
    // path with the suggested ".mp4" extension dropped -- force it back on so
    // the exported file is visibly an .mp4 rather than relying solely on
    // VideoExporter forcing the container format regardless of filename.
    juce::File withMp4Extension(juce::File file)
    {
        if (! file.hasFileExtension("mp4"))
            file = file.withFileExtension("mp4");

        return file;
    }

    juce::String describeSelection(std::size_t exported, std::size_t skipped,
                                   const std::vector<PlaylistVideoExporter::SkippedTrack>& reasons)
    {
        juce::String summary = juce::String((int) exported) + " of "
                             + juce::String((int) (exported + skipped)) + " playlist tracks will be exported.";

        if (! reasons.empty())
        {
            summary << "\n\nSkipped:\n";
            for (const auto& reason : reasons)
                summary << "- " << reason.name << " (" << reason.reason << ")\n";
        }

        return summary << "\nContinue?";
    }
}

PlaylistVideoExportController::PlaylistVideoExportController(AudioEngine& engine, PlaylistManager& playlist,
                                                            VisualizationComponent& visualizerComponent)
    : audioEngine(engine),
      playlistManager(playlist),
      visualizer(visualizerComponent)
{
}

PlaylistVideoExportController::~PlaylistVideoExportController()
{
    // Dismiss the progress window first: its modal callback cancels the export
    // and drops its reference to us.
    if (progressWindow != nullptr)
        progressWindow->exitModalState(0);

    exporter.reset();
}

void PlaylistVideoExportController::showExportDialog()
{
    std::vector<PlaylistVideoExporter::TrackSpec> baseTracks;
    std::vector<PlaylistVideoExporter::SkippedTrack> baseSkipped;
    PlaylistVideoExporter::selectTracks(playlistManager.getItems(),
                                        VideoExporter::ExportMode::Visualization,
                                        baseTracks, baseSkipped);

    if (baseTracks.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Playlist Video",
            "Add at least one local audio file or .xtp/.xtd song to the playlist first.");
        return;
    }

    if (! VideoExporter::isFfmpegAvailable())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Playlist Video",
            "ffmpeg was not found on PATH. Install ffmpeg to enable video export.");
        return;
    }

    const bool hasPatternData = playlistHasPatternData(playlistManager.getItems());

    fileChooser = std::make_unique<juce::FileChooser>(
        "Export playlist video as...",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("Playlist.mp4"),
        "*.mp4");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, hasPatternData](const juce::FileChooser& fc)
        {
            auto outputFile = fc.getResult();
            if (outputFile == juce::File{})
                return;

            outputFile = withMp4Extension(outputFile);

            auto dialog = std::make_unique<VideoExportSettingsDialog>(hasPatternData, /*isPlaylistMode*/ true);
            auto* dialogPtr = dialog.get();
            dialogPtr->onExport = [this, dialogPtr, outputFile]
            {
                auto optionsTemplate = dialogPtr->buildOptions();
                auto settings = dialogPtr->buildPlaylistSettings();

                // The mode the user picked decides which tracks qualify, so the
                // candidate list is rebuilt from it rather than reused.
                std::vector<PlaylistVideoExporter::TrackSpec> tracks;
                std::vector<PlaylistVideoExporter::SkippedTrack> skipped;
                PlaylistVideoExporter::selectTracks(playlistManager.getItems(), optionsTemplate.mode,
                                                    tracks, skipped);

                confirmAndStart(std::move(tracks), std::move(skipped),
                                std::move(optionsTemplate), std::move(settings), outputFile);
            };
            VideoExportSettingsDialog::launch(std::move(dialog));
        });
}

void PlaylistVideoExportController::confirmAndStart(std::vector<PlaylistVideoExporter::TrackSpec> tracks,
                                                   std::vector<PlaylistVideoExporter::SkippedTrack> skipped,
                                                   VideoExporter::Options optionsTemplate,
                                                   PlaylistVideoExporter::Settings settings,
                                                   juce::File outputFile)
{
    if (tracks.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Playlist Video",
            "No tracks in the playlist are compatible with the chosen mode.");
        return;
    }

    const auto trackCount = tracks.size();
    const auto skippedCount = skipped.size();
    const auto summary = describeSelection(trackCount, skippedCount, skipped);

    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, "Export Playlist Video",
        summary, "Export", "Cancel", nullptr,
        juce::ModalCallbackFunction::create(
            [this, tracks, optionsTemplate, settings, outputFile](int result) mutable
            {
                if (result == 1)
                    startExport(std::move(tracks), std::move(optionsTemplate),
                                std::move(settings), outputFile);
            }));
}

void PlaylistVideoExportController::startExport(std::vector<PlaylistVideoExporter::TrackSpec> tracks,
                                               VideoExporter::Options optionsTemplate,
                                               PlaylistVideoExporter::Settings settings,
                                               juce::File outputFile)
{
    // Same reasoning as the single-track export: the export thread gets a fresh
    // plugin instance of the tab the live visualizer is showing, plus a snapshot
    // of the current EQ gains, so it shares no state with the UI.
    const int pluginIndex = visualizer.getActivePluginIndex();
    optionsTemplate.pluginFactory = visualizer.makePluginFactory(pluginIndex);
    optionsTemplate.equalizerGainsDb = audioEngine.getEqualizer().getAllGains();

    exporter = std::make_unique<PlaylistVideoExporter>(
        std::move(tracks), std::move(optionsTemplate), settings, outputFile);

    progressValue = 0.0;
    progressWindow = new juce::AlertWindow("Exporting Playlist Video",
        "Starting...", juce::AlertWindow::NoIcon);

    progressBar = std::make_unique<juce::ProgressBar>(progressValue);
    progressBar->setSize(300, 24);
    progressWindow->addCustomComponent(progressBar.get());
    progressWindow->addButton("Cancel", 0);
    progressWindow->enterModalState(true,
        juce::ModalCallbackFunction::create([this](int)
        {
            // Cancel button, or the window being dismissed by other means --
            // either way the export thread has to be told to stop.
            if (exporter != nullptr)
                exporter->cancel();

            progressWindow = nullptr;
            progressBar.reset();
        }),
        true);

    exporter->startExport();
}

void PlaylistVideoExportController::pollProgress()
{
    if (exporter == nullptr)
        return;

    progressValue = exporter->getProgress();

    if (progressWindow != nullptr)
        progressWindow->setMessage(exporter->getCurrentTrackLabel());

    if (! exporter->hasFinished())
        return;

    exporter->waitForThreadToExit(2000);

    const auto result  = exporter->getResult();
    const auto message = exporter->getResultMessage();

    exporter.reset();
    closeProgressWindow();

    if (result != VideoExporter::Result::Cancelled)
        juce::AlertWindow::showMessageBoxAsync(
            result == VideoExporter::Result::Success ? juce::AlertWindow::InfoIcon
                                                     : juce::AlertWindow::WarningIcon,
            "Export Playlist Video", message);
}

void PlaylistVideoExportController::closeProgressWindow()
{
    if (progressWindow != nullptr)
    {
        progressWindow->exitModalState(0);
        progressWindow = nullptr; // self-deletes (deleteWhenDismissed = true)
    }

    progressBar.reset();
}
