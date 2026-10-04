#include "FullScreenWindowController.h"
#include "../Audio/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"
#include "FullScreenHostWindow.h"
#include "FullScreenVisualizerWindow.h"
#include "ModulePatternViewer.h"
#include "VideoPreviewComponent.h"
#include "VisualizationComponent.h"

FullScreenWindowController::FullScreenWindowController(AudioEngine& engine, PlaylistManager& playlist,
                                                       VisualizationComponent& visualizerComponent,
                                                       ModulePatternViewer& pattern,
                                                       VideoPreviewComponent& video)
    : audioEngine(engine),
      playlistManager(playlist),
      visualizer(visualizerComponent),
      patternViewer(pattern),
      videoPreview(video)
{
}

FullScreenWindowController::~FullScreenWindowController()
{
    // Dropping the windows reparents whatever they were hosting back where it
    // came from (see FullScreenHostWindow::~FullScreenHostWindow()), so the
    // embedded views are left in a usable state.
    videoWindow.reset();
    patternWindow.reset();
    visualizerWindow.reset();
}

void FullScreenWindowController::openVisualizer()
{
    if (visualizerWindow != nullptr)
        return; // already open

    // Opens on the same tab the embedded view is showing; the callback runs
    // asynchronously, after the Escape key event that triggered it has been
    // dispatched, so resetting from inside it cannot delete the window mid-event.
    visualizerWindow = std::make_unique<FullScreenVisualizerWindow>(
        &audioEngine, &playlistManager, visualizer.getActivePluginIndex(),
        [this] { visualizerWindow.reset(); });

    visualizerWindow->show();
}

void FullScreenWindowController::openPatternViewer()
{
    if (patternWindow != nullptr || ! patternViewer.isVisible())
        return; // already open, or no module loaded to show

    patternWindow = std::make_unique<FullScreenHostWindow>(
        patternViewer, &audioEngine, &playlistManager,
        [this] { patternWindow.reset(); });

    patternWindow->show();
}

void FullScreenWindowController::closePatternViewer()
{
    patternWindow.reset();
}

void FullScreenWindowController::openVideo()
{
    if (videoWindow != nullptr || ! videoPreview.isVisible())
        return; // already open, or no video loaded to show

    // hideCursor: fullscreen video has nothing to point at, and transport is
    // all keyboard shortcuts.
    videoWindow = std::make_unique<FullScreenHostWindow>(
        videoPreview, &audioEngine, &playlistManager,
        [this] { videoWindow.reset(); }, true);

    videoWindow->show();
}

void FullScreenWindowController::closeVideo()
{
    videoWindow.reset();
}
