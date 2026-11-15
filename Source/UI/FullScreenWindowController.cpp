#include "FullScreenWindowController.h"
#include "../Audio/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"
#include "FullScreenHostWindow.h"
#include "FullScreenVisualizerWindow.h"
#include "ModulePatternViewer.h"
#include "SequencerSidePanel.h"
#include "FullScreenTransportBar.h"
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
      videoPreview(video),
      sequencerPanelSections(SequencerSidePanel::defaultSections),
      sequencerPanelShare(FullScreenHostWindow::kDefaultSidePanelShare)
{
}

FullScreenWindowController::~FullScreenWindowController()
{
    // Dropping the windows reparents whatever they were hosting back where it
    // came from (see FullScreenHostWindow::~FullScreenHostWindow()), so the
    // embedded views are left in a usable state.
    videoWindow.reset();
    resetPatternWindow();
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
        [this] { visualizerWindow.reset(); updateMainVisualizerAnimation(); });

    visualizerWindow->show();
    updateMainVisualizerAnimation();
}

void FullScreenWindowController::openPatternViewer()
{
    if (patternWindow != nullptr || ! patternViewer.isVisible())
        return; // already open, or no module loaded to show

    patternWindow = std::make_unique<FullScreenHostWindow>(
        patternViewer, &audioEngine, &playlistManager,
        [this] { resetPatternWindow(); });

    // The side panel sits right of the grid at a fixed (draggable) width.
    auto panel = std::make_unique<SequencerSidePanel>(audioEngine, playlistManager);
    auto* panelPtr = panel.get();
    panel->setVisibleSections(sequencerPanelSections);
    panel->onVisibleSectionsChanged = [this](int mask) { sequencerPanelSections = mask; };
    auto transport = std::make_unique<FullScreenTransportBar>(audioEngine, playlistManager);
    auto* transportPtr = transport.get();
    patternWindow->onExtraKeyPress = [panelPtr, transportPtr](const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::pageUpKey)   { transportPtr->skipPattern(-1); return true; }
        if (key == juce::KeyPress::pageDownKey) { transportPtr->skipPattern(1);  return true; }
        return panelPtr->isVisible() && panelPtr->handleKeyPress(key);
    };
    patternWindow->setSidePanel(std::move(panel), sequencerPanelShare, sequencerPanelShown,
                                [this](bool shown, float share)
                                {
                                    sequencerPanelShown = shown;
                                    sequencerPanelShare = share;
                                });

    patternWindow->setBottomBar(std::move(transport), FullScreenTransportBar::preferredHeight);

    patternWindow->show();
    updateMainVisualizerAnimation();
}

void FullScreenWindowController::closePatternViewer()
{
    resetPatternWindow();
}

void FullScreenWindowController::resetPatternWindow()
{
    patternWindow.reset();
    updateMainVisualizerAnimation();
}

void FullScreenWindowController::updateMainVisualizerAnimation()
{
    visualizer.setAnimationSuspended(visualizerWindow != nullptr || patternWindow != nullptr
                                     || videoWindow != nullptr);
}

void FullScreenWindowController::openVideo()
{
    if (videoWindow != nullptr || ! videoPreview.isVisible())
        return; // already open, or no video loaded to show

    // hideCursor: fullscreen video has nothing to point at, and transport is
    // all keyboard shortcuts.
    videoWindow = std::make_unique<FullScreenHostWindow>(
        videoPreview, &audioEngine, &playlistManager,
        [this] { videoWindow.reset(); updateMainVisualizerAnimation(); }, true);

    videoWindow->show();
    updateMainVisualizerAnimation();
}

void FullScreenWindowController::closeVideo()
{
    videoWindow.reset();
    updateMainVisualizerAnimation();
}
