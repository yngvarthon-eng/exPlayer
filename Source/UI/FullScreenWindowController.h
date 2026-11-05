#pragma once

#include <juce_core/juce_core.h>
#include <memory>

class AudioEngine;
class PlaylistManager;
class VisualizationComponent;
class ModulePatternViewer;
class VideoPreviewComponent;
class FullScreenVisualizerWindow;
class FullScreenHostWindow;

// Owns the three kiosk-mode windows the main window can open: the visualizer
// mirror (a window with its own fresh VisualizationComponent, so the embedded
// view keeps running untouched) and the fullscreen sequencer and video views
// (which reparent the existing component rather than creating a second one, so
// neither decode state nor playback position is duplicated).
//
// All three are top-level desktop windows rather than children, so their
// lifetime is not tied to the panel they were opened from -- the controller is
// what closes them, and what the rest of the UI asks when it needs to know
// whether one is currently up.
class FullScreenWindowController
{
public:
    FullScreenWindowController(AudioEngine& engine, PlaylistManager& playlist,
                               VisualizationComponent& visualizer,
                               ModulePatternViewer& patternViewer,
                               VideoPreviewComponent& videoPreview);
    ~FullScreenWindowController();

    void openVisualizer();

    // Reparents the pattern viewer fullscreen. Does nothing when no module is
    // loaded, as the viewer would have nothing to show.
    void openPatternViewer();

    // Drops the fullscreen sequencer view if it is open. Used when the loaded
    // track stops being a module, so no blank fullscreen window is left behind.
    void closePatternViewer();

    // Reparents the video preview fullscreen. Does nothing when no video is
    // loaded.
    void openVideo();

    // Drops the fullscreen video view if it is open -- same reasoning as
    // closePatternViewer() when the playlist moves on to a non-video track.
    void closeVideo();

private:
    AudioEngine& audioEngine;
    PlaylistManager& playlistManager;
    VisualizationComponent& visualizer;
    ModulePatternViewer& patternViewer;
    VideoPreviewComponent& videoPreview;

    std::unique_ptr<FullScreenVisualizerWindow> visualizerWindow;
    std::unique_ptr<FullScreenHostWindow> patternWindow;
    std::unique_ptr<FullScreenHostWindow> videoWindow;

    // Drops the sequencer window.
    void resetPatternWindow();

    // The sequencer view's side panel (SequencerSidePanel) is rebuilt on every
    // opening; these carry its toggles over from one opening to the next.
    int   sequencerPanelSections;
    bool  sequencerPanelShown = true;
    float sequencerPanelShare;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FullScreenWindowController)
};
