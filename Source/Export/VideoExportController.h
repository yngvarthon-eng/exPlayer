#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "VideoExporter.h"

class AudioEngine;
class PlaylistManager;
class VisualizationComponent;

// Drives video export from the UI: checks the current track can be exported,
// asks where to put the file, collects the render settings, runs the export and
// reports the outcome.
//
// VideoExporter deliberately has no completion callback (its thread would race
// the message thread), so this controller is polled from its owner's timer:
// pollProgress() advances the modal progress window and tears it down, showing
// the result, once the export thread has finished.
class VideoExportController
{
public:
    VideoExportController(AudioEngine& engine, PlaylistManager& playlist,
                          VisualizationComponent& visualizer);
    ~VideoExportController();

    // Exports the playlist row at highlightedIndex when it is valid, otherwise
    // the current (playing) track. Explains why and returns immediately when
    // there is nothing exportable (no track, a network stream, or no ffmpeg on
    // PATH); otherwise opens the save dialog followed by the render settings
    // dialog.
    void showExportDialog(int highlightedIndex = -1);

    // Advances the progress window, and closes it with a result message once the
    // export has finished. Call from a timer while an export is running.
    void pollProgress();

    bool isExporting() const { return exporter != nullptr; }

private:
    void startExport(VideoExporter::Options options);
    void closeProgressWindow();

    AudioEngine& audioEngine;
    PlaylistManager& playlistManager;
    VisualizationComponent& visualizer;

    std::unique_ptr<VideoExporter> exporter;
    std::unique_ptr<juce::FileChooser> fileChooser;

    double progressValue = 0.0;
    std::unique_ptr<juce::ProgressBar> progressBar;

    // A self-deleting AlertWindow (deleteWhenDismissed = true), so it is tracked
    // as a nullable raw pointer rather than owned.
    juce::AlertWindow* progressWindow = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoExportController)
};
