#include "VideoExportController.h"
#include "../Audio/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"
#include "../UI/VideoExportSettingsDialog.h"
#include "../UI/VisualizationComponent.h"

VideoExportController::VideoExportController(AudioEngine& engine, PlaylistManager& playlist,
                                            VisualizationComponent& visualizer)
    : audioEngine(engine),
      playlistManager(playlist),
      visualizer(visualizer)
{
}

VideoExportController::~VideoExportController()
{
    // Dismiss the progress window first: its modal callback cancels the export
    // and drops its reference to us.
    if (progressWindow != nullptr)
        progressWindow->exitModalState(0);

    exporter.reset();
}

void VideoExportController::showExportDialog(int highlightedIndex)
{
    const auto& items = playlistManager.getItems();
    const auto* item = juce::isPositiveAndBelow(highlightedIndex, items.size())
                           ? &items.getReference(highlightedIndex)
                           : playlistManager.getCurrentItem();

    if (item == nullptr || item->isStream)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Video",
            "Select a local audio file or .xtp/.xtd song in the playlist first.");
        return;
    }

    if (! VideoExporter::isFfmpegAvailable())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Video",
            "ffmpeg was not found on PATH. Install ffmpeg to enable video export.");
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser>(
        "Export video as...",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile(item->name + ".mp4"),
        "*.mp4");

    const auto sourceFile = item->file;
    const bool isXtp = item->isXtpSequence;
    const bool hasPatternData = item->isXtpSequence || item->isModule;

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, sourceFile, isXtp, hasPatternData](const juce::FileChooser& fc)
        {
            auto outputFile = fc.getResult();
            if (outputFile == juce::File{})
                return;

            // Some save-dialog paths on Linux (zenity, in particular) can hand back a
            // path with the suggested ".mp4" extension dropped -- force it back on so
            // the exported file is visibly an .mp4 rather than relying solely on
            // VideoExporter forcing the container format regardless of filename.
            if (! outputFile.hasFileExtension("mp4"))
                outputFile = outputFile.withFileExtension("mp4");

            auto dialog = std::make_unique<VideoExportSettingsDialog>(hasPatternData);
            auto* dialogPtr = dialog.get();
            dialogPtr->onExport = [this, dialogPtr, sourceFile, isXtp, outputFile]
            {
                VideoExporter::Options options = dialogPtr->buildOptions();
                options.sourceFile = sourceFile;
                options.isXtpSequence = isXtp;
                options.outputFile = outputFile;
                startExport(std::move(options));
            };
            VideoExportSettingsDialog::launch(std::move(dialog));
        });
}

void VideoExportController::pollProgress()
{
    if (exporter == nullptr)
        return;

    progressValue = exporter->getProgress();

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
            "Export Video", message);
}

void VideoExportController::startExport(VideoExporter::Options options)
{
    // The export thread gets a fresh plugin instance of whatever tab the live
    // visualizer is showing, plus a snapshot of the current EQ gains, so it
    // never shares state with the UI it was launched from.
    const int pluginIndex = visualizer.getActivePluginIndex();
    options.pluginFactory = visualizer.makePluginFactory(pluginIndex);
    options.equalizerGainsDb = audioEngine.getEqualizer().getAllGains();

    const auto sourceFile = options.sourceFile;
    exporter = std::make_unique<VideoExporter>(std::move(options));

    progressValue = 0.0;
    progressWindow = new juce::AlertWindow(
        "Exporting Video",
        "Rendering \"" + sourceFile.getFileNameWithoutExtension() + "\"...",
        juce::AlertWindow::NoIcon);

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

void VideoExportController::closeProgressWindow()
{
    if (progressWindow != nullptr)
    {
        progressWindow->exitModalState(0);
        progressWindow = nullptr; // self-deletes (deleteWhenDismissed = true)
    }

    progressBar.reset();
}
