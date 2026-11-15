#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Audio/AudioEngine.h"
#include "Playlist/PlaylistManager.h"
#include "UI/PlayerControlsComponent.h"
#include "UI/PlaylistEditorComponent.h"
#include "UI/VisualizationComponent.h"
#include "UI/VideoPreviewComponent.h"
#include "UI/MainToolbar.h"
#include "UI/MainLayout.h"
#include "UI/FullScreenWindowController.h"
#include "UI/ModulePatternViewer.h"
#include "Effects/EqualizerComponent.h"
#include "Plugins/PluginHostManager.h"
#include "UI/PluginChainComponent.h"
#include "UI/AudioAnalysisComponent.h"
#include "Export/VideoExportController.h"
#include "Export/PlaylistVideoExportController.h"
#include "UI/MicRecorderComponent.h"

// The application window: owns the engine, the playlist and every panel, and
// wires them together.
//
// It is deliberately only a coordinator -- each panel supplies its own controls
// and behaviour (MainToolbar, PluginChainComponent, EqualizerComponent, ...),
// while the jobs that used to live here have their own units that this class
// simply owns and drives:
//   * audio quality + panel toggles   -> MainToolbar
//   * the three fullscreen windows    -> FullScreenWindowController
//   * single-track video export       -> VideoExportController
//   * whole-playlist video export     -> PlaylistVideoExportController
//   * drag-and-drop file filtering    -> AudioFileCollector
// What remains here is window-level policy: which panel is visible for the
// current track, how the window is laid out, and routing one component's
// request to the component that handles it.
class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::AsyncUpdater,
                      private juce::Timer,
                      private juce::KeyListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originator) override;

    // Add the given file paths to the playlist and start playing the first one.
    // Non-existent paths are ignored. Used for command-line / file-association opens.
    void openFiles(const juce::StringArray& paths);

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    // FileDragAndDropTarget — drop audio files or folders anywhere in the window
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Public so it can be invoked via PlayerControlsComponent's record menu
    // ("Record video with exPlayer's export function"), not just a toolbar
    // button of its own.
    void showExportVideoDialog();

    // Public for the same reason ("Record playlist as video" in the record menu).
    void showExportPlaylistVideoDialog();

    // Opens (or brings forward) the "Record from microphone" window.
    void showMicRecorder();

private:
    // --- Construction, in the order the constructor calls these -------------

    // Instantiates the engine, every panel and the three controllers that drive
    // them, and parents the panels to this window.
    void createPanels();

    // Points each panel's request callback at the panel or controller that
    // serves it (the toolbar's fullscreen launchers, the transport's record
    // menu, the equalizer's resize request, the MIDI input menu).
    void connectPanelRequests();

    // --- Panel policy -------------------------------------------------------

    // Applies the shared panel state to every panel that follows it, then
    // re-lays out. Called when a toolbar toggle is switched or the loaded track
    // changes. Geometry and visibility are both derived from
    // currentPanelState(), so the two can never disagree.
    void applyPanelVisibility();

    // The flags both the visibility policy and the layout work from: the
    // toolbar's collapse toggles, the current track's kind, and whichever panel
    // state is only known to the live components.
    MainLayout::PanelState currentPanelState() const;

    // Shows whichever of the video / visualizer / pattern panels fits the
    // currently loaded track, and closes any fullscreen view that stopped being
    // valid. Runs after the playlist changes.
    void updatePanelsForCurrentTrack();

    // Appends the given files to the playlist and selects the first one added,
    // starting it when startOnlyIfIdle is false or nothing is playing yet.
    // Shared by openFiles() and filesDropped(), which differ only in that
    // choice.
    void addFilesAndSelectFirstNew (const juce::Array<juce::File>& files, bool startOnlyIfIdle);

    // False until createPanels() has run: resized() can be reached from a
    // child's callback (EqualizerComponent reports its expansion) before the
    // later panels exist.
    bool panelsAreReady() const;

    // playlistManager must be declared before audioEngine so it is destroyed AFTER audioEngine
    std::unique_ptr<PlaylistManager>            playlistManager;
    std::unique_ptr<AudioEngine>                audioEngine;

    // Toolbar: collapse toggles, fullscreen launchers, output quality selectors.
    std::unique_ptr<MainToolbar>                mainToolbar;

    std::unique_ptr<PlayerControlsComponent>    playerControls;
    std::unique_ptr<PlaylistEditorComponent>    playlistEditor;
    std::unique_ptr<VisualizationComponent>     visualization;
    std::unique_ptr<VideoPreviewComponent>      videoPreview;
    std::unique_ptr<ModulePatternViewer>        patternViewer;
    std::unique_ptr<EqualizerComponent>         equalizerUI;
    std::unique_ptr<PluginHostManager>          pluginHostManager;
    std::unique_ptr<PluginChainComponent>       pluginChainUI;

    // On-screen keyboard for playing any synth loaded into the FX chain (e.g. the
    // user's own TB-303/Minimoog VST3 emulators) — bound to AudioEngine's shared
    // MidiKeyboardState, which is drained into the chain every audio callback.
    std::unique_ptr<juce::MidiKeyboardComponent> midiKeyboard;
    // Opens a checklist of available hardware MIDI inputs (see
    // AudioEngine::setMidiInputEnabled) — nothing is auto-enabled, since a
    // controller visible under more than one driver/port would otherwise double-
    // deliver every note.
    std::unique_ptr<juce::TextButton>           midiInputBtn;

    std::unique_ptr<AudioAnalysisComponent>     analysisUI;

    // Declared after every component they host or quote: tearing these down
    // reparents the hosted panels back into this window, so those components
    // have to still be alive at that point.
    std::unique_ptr<FullScreenWindowController>    fullscreenWindows;
    std::unique_ptr<VideoExportController>         videoExport;
    std::unique_ptr<PlaylistVideoExportController> playlistVideoExport;

    // Created on demand and destroyed when closed, so the mic is only held
    // open while the window is up. Declared after audioEngine, whose
    // auxiliary source it borrows, so it is torn down first.
    std::unique_ptr<MicRecorderWindow> micRecorderWindow;

    // True while the loaded track is a video. Kept as a flag rather than read
    // back from videoPreview->isVisible() so the layout and the video handoff
    // never depend on the order in which they are applied.
    bool currentTrackIsVideo = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
