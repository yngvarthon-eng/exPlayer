#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PlaylistVideoExporter.h"

class AudioEngine;
class PlaylistManager;
class VisualizationComponent;

// The playlist counterpart of VideoExportController: checks the playlist has
// something exportable, asks where to put the file, collects the render
// settings, works out which tracks a given mode actually supports, confirms the
// resulting selection with the user, runs the export, and reports the outcome.
//
// Like its single-track sibling it polls rather than waiting on a callback --
// PlaylistVideoExporter runs on its own juce::Thread and deliberately has no
// completion callback, so pollProgress() is what advances and finally closes
// the modal progress window.
class PlaylistVideoExportController
{
public:
    PlaylistVideoExportController(AudioEngine& engine, PlaylistManager& playlist,
                                  VisualizationComponent& visualizer);
    ~PlaylistVideoExportController();

    // Explains why and returns immediately when the playlist has nothing
    // exportable, or ffmpeg is missing; otherwise opens the save dialog,
    // followed by the render settings dialog and the track-selection summary.
    void showExportDialog();

    // Advances the progress window, and closes it with a result message once the
    // export has finished. Call from a timer while an export is running.
    void pollProgress();

    bool isExporting() const { return exporter != nullptr; }

private:
    // Reports how many playlist tracks the chosen mode can export, and why the
    // rest cannot, then starts (or abandons) the export on the user's answer.
    void confirmAndStart(std::vector<PlaylistVideoExporter::TrackSpec> tracks,
                         std::vector<PlaylistVideoExporter::SkippedTrack> skipped,
                         VideoExporter::Options optionsTemplate,
                         PlaylistVideoExporter::Settings settings,
                         juce::File outputFile);

    void startExport(std::vector<PlaylistVideoExporter::TrackSpec> tracks,
                     VideoExporter::Options optionsTemplate,
                     PlaylistVideoExporter::Settings settings,
                     juce::File outputFile);

    void closeProgressWindow();

    AudioEngine& audioEngine;
    PlaylistManager& playlistManager;
    VisualizationComponent& visualizer;

    std::unique_ptr<PlaylistVideoExporter> exporter;
    std::unique_ptr<juce::FileChooser> fileChooser;

    double progressValue = 0.0;
    std::unique_ptr<juce::ProgressBar> progressBar;

    // A self-deleting AlertWindow (deleteWhenDismissed = true), so it is tracked
    // as a nullable raw pointer rather than owned.
    juce::AlertWindow* progressWindow = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistVideoExportController)
};
