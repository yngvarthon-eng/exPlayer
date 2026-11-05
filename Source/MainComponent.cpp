#include "MainComponent.h"
#include "UI/MidiInputMenu.h"
#include "UI/TransportShortcuts.h"
#include "Playlist/AudioFileCollector.h"
#include "Plugins/PluginChainPersistence.h"

namespace
{
    // How often the pattern viewer's playback cursor and the exporters'
    // progress are refreshed. 25 ms is 40 Hz.
    constexpr int uiRefreshIntervalMs = 25;

    constexpr int initialWindowWidth  = 1200;
    constexpr int initialWindowHeight = 800;
}

// ==============================================================================
// Construction
//
// The window is built in three steps so each can be read on its own: create the
// panels, point their request callbacks at whatever serves them, then bring the
// audio side up (device, restored FX chain) now that every panel exists.
// ==============================================================================

MainComponent::MainComponent()
{
    createPanels();
    connectPanelRequests();

    playlistManager->addChangeListener (this);

    setSize (initialWindowWidth, initialWindowHeight);
    setWantsKeyboardFocus (true);
    addKeyListener (this);

    // Aligns the panels with the toolbar's (all open) toggles, once the window
    // has a size for the layout to work with.
    applyPanelVisibility();

    audioEngine->initialise();
    mainToolbar->refreshAudioQuality(); // after the device is open, so we know its real rates

    // Restore the FX chain saved by PluginChainComponent's autosave -- must come after
    // initialise() so PluginChain::prepare() has already run and knows the real
    // sample rate/block size to instantiate plugins against.
    PluginChainPersistence::load (audioEngine->getPluginChain(), *pluginHostManager);
    pluginChainUI->refreshFromChain();

    startTimer (uiRefreshIntervalMs);

    grabKeyboardFocus(); // so transport shortcuts work before the user clicks anything
}

void MainComponent::createPanels()
{
    playlistManager   = std::make_unique<PlaylistManager>();
    audioEngine       = std::make_unique<AudioEngine> (playlistManager.get());
    pluginHostManager = std::make_unique<PluginHostManager>();
    pluginHostManager->initialise();

    mainToolbar    = std::make_unique<MainToolbar> (*audioEngine);
    playerControls = std::make_unique<PlayerControlsComponent> (audioEngine.get(), playlistManager.get());
    playlistEditor = std::make_unique<PlaylistEditorComponent> (playlistManager.get());
    visualization  = std::make_unique<VisualizationComponent> (audioEngine.get());
    videoPreview   = std::make_unique<VideoPreviewComponent> (audioEngine.get());
    patternViewer  = std::make_unique<ModulePatternViewer>();
    equalizerUI    = std::make_unique<EqualizerComponent> (audioEngine.get());

    pluginChainUI = std::make_unique<PluginChainComponent> (
                        pluginHostManager.get(), &audioEngine->getPluginChain(), *audioEngine);
    midiKeyboard  = std::make_unique<juce::MidiKeyboardComponent> (
                        audioEngine->getMidiKeyboardState(),
                        juce::MidiKeyboardComponent::horizontalKeyboard);
    midiInputBtn  = std::make_unique<juce::TextButton> ("MIDI In");
    analysisUI    = std::make_unique<AudioAnalysisComponent>();

    // Built last of the component-owning members: these hold references to the
    // panels above (and reparent them while a fullscreen view is open), so they
    // must also be destroyed first.
    fullscreenWindows   = std::make_unique<FullScreenWindowController> (
                              *audioEngine, *playlistManager, *visualization, *patternViewer, *videoPreview);
    videoExport         = std::make_unique<VideoExportController> (
                              *audioEngine, *playlistManager, *visualization);
    playlistVideoExport = std::make_unique<PlaylistVideoExportController> (
                              *audioEngine, *playlistManager, *visualization);

    addAndMakeVisible (*mainToolbar);
    addAndMakeVisible (*playerControls);
    addAndMakeVisible (*visualization);
    addAndMakeVisible (*playlistEditor);
    addAndMakeVisible (*equalizerUI);
    addAndMakeVisible (*pluginChainUI);
    addAndMakeVisible (*midiKeyboard);
    addAndMakeVisible (*midiInputBtn);
    addAndMakeVisible (*analysisUI);

    // Nothing is playing yet, so neither of these has content to show. They are
    // parented but not made visible; updatePanelsForCurrentTrack() is what shows
    // them once a module or a video is loaded.
    addChildComponent (*patternViewer);
    addChildComponent (*videoPreview);
}

void MainComponent::connectPanelRequests()
{
    // Toolbar: re-lay out when a toggle changes, and hand the three fullscreen
    // launchers to the controller that owns those windows.
    mainToolbar->onLayoutChanged               = [this] { applyPanelVisibility(); };
    mainToolbar->onOpenFullscreenVisualizer    = [this] { fullscreenWindows->openVisualizer(); };
    mainToolbar->onOpenFullscreenPatternViewer = [this] { fullscreenWindows->openPatternViewer(); };
    mainToolbar->onOpenFullscreenVideo         = [this] { fullscreenWindows->openVideo(); };

    // Transport: the record menu's two video-export entries.
    playerControls->onRequestVideoExport         = [this] { showExportVideoDialog(); };
    playerControls->onRequestPlaylistVideoExport = [this] { showExportPlaylistVideoDialog(); };

    // The 30-band equalizer view needs more room than the 10-band one.
    equalizerUI->onExpandedChanged = [this] { resized(); };

    midiInputBtn->onClick = [this] { MidiInputMenu::show (*audioEngine, *midiInputBtn); };
}

MainComponent::~MainComponent()
{
    stopTimer();
    removeKeyListener (this);
    playlistManager->removeChangeListener (this);
}

// ==============================================================================
// Panel state: what is visible, and where everything goes
// ==============================================================================

MainLayout::PanelState MainComponent::currentPanelState() const
{
    MainLayout::PanelState state;

    // Toolbar toggles.
    state.playlistCollapsed = mainToolbar->isPlaylistCollapsed();
    state.eqCollapsed       = mainToolbar->isEqCollapsed();
    state.vizCollapsed      = mainToolbar->isVizCollapsed();
    state.fxCollapsed       = mainToolbar->isFxCollapsed();
    state.analysisCollapsed = mainToolbar->isAnalysisCollapsed();

    // Read back from the panels themselves wherever a decision is theirs, so the
    // layout can never disagree with what the user sees.
    state.equalizerExpanded    = equalizerUI->isExpanded();
    state.patternViewerVisible = patternViewer->isVisible();
    state.videoVisible         = currentTrackIsVideo;

    return state;
}

void MainComponent::applyPanelVisibility()
{
    if (! panelsAreReady())
        return;

    const auto visible = MainLayout::computeVisibility (currentPanelState());

    playlistEditor->setVisible (visible.playlistEditor);
    equalizerUI->setVisible (visible.equalizer);
    analysisUI->setVisible (visible.analysis);
    pluginChainUI->setVisible (visible.fxChain);
    midiKeyboard->setVisible (visible.midiKeyboard);
    midiInputBtn->setVisible (visible.midiButton);
    visualization->setVisible (visible.visualization);
    videoPreview->setVisible (visible.videoPreview);

    // The pattern viewer is deliberately not part of that set: it follows the
    // loaded track (a module is playing or it is not), which
    // updatePanelsForCurrentTrack() owns.

    resized(); // the panels that just appeared or disappeared change the layout
}

bool MainComponent::panelsAreReady() const
{
    return playerControls && mainToolbar && playlistEditor && equalizerUI && analysisUI
        && pluginChainUI && midiKeyboard && midiInputBtn && visualization && videoPreview
        && patternViewer;
}

// ==============================================================================
// Track changes
// ==============================================================================

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source != playlistManager.get())
        return;

    // The pattern data belongs to whatever was playing a moment ago; drop it
    // now (rather than in handleAsyncUpdate) so the viewer can never draw a
    // stale pattern for the track that is about to replace it.
    patternViewer->setPatternData (nullptr);
    patternViewer->setVisible (false);
    triggerAsyncUpdate();
}

void MainComponent::handleAsyncUpdate()
{
    updatePanelsForCurrentTrack();
}

void MainComponent::updatePanelsForCurrentTrack()
{
    const bool showPatternViewer = audioEngine->lastLoadedWasModule();
    const auto& moduleData = audioEngine->getModulePatternData();

    patternViewer->setPatternData (showPatternViewer ? &moduleData : nullptr);
    patternViewer->setVisible (showPatternViewer);
    mainToolbar->setPatternFullscreenEnabled (showPatternViewer);

    // If the loaded track stopped being a module while the pattern view was
    // fullscreen (e.g. the playlist advanced to an audio file), close it rather
    // than leaving a blank fullscreen window up.
    if (! showPatternViewer)
        fullscreenWindows->closePatternViewer();

    // Video and the spectrum visualizer share the same panel slot and are
    // mutually exclusive in this version -- not stacked.
    auto* item = playlistManager->getCurrentItem();
    currentTrackIsVideo = item != nullptr && item->isVideo;

    if (currentTrackIsVideo)
        videoPreview->setVideoFile (item->file);
    else
        videoPreview->clearVideo();

    mainToolbar->setVideoFullscreenEnabled (currentTrackIsVideo);

    if (! currentTrackIsVideo)
        fullscreenWindows->closeVideo();

    // Trigger audio analysis for file tracks; streams/videos are not analyzable
    // through this native-decode-based pipeline.
    if (item != nullptr && ! item->isStream && ! item->isVideo && item->file.existsAsFile())
        analysisUI->analyzeFile (item->file);
    else
        analysisUI->clear();

    applyPanelVisibility();
}

// ==============================================================================
// Track inputs: command line, file association, drag and drop
// ==============================================================================

void MainComponent::openFiles (const juce::StringArray& paths)
{
    // Only paths that exist are considered here -- unlike a drop, which is run
    // through AudioFileCollector, a path handed to the application is taken as
    // the user naming that exact file.
    juce::Array<juce::File> existing;
    for (const auto& path : paths)
    {
        juce::File file (path);
        if (file.existsAsFile())
            existing.add (file);
    }

    // Always start the first one: this is a deliberate open, not a queue behind
    // whatever happens to be playing.
    addFilesAndSelectFirstNew (existing, false);
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        juce::File f (path);
        if (f.isDirectory() || AudioFileCollector::isSupported (f))
            return true;
    }
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int /*x*/, int /*y*/)
{
    // A drop is a queue: folders are expanded and anything the engine cannot
    // decode is dropped, and a track already playing is left alone.
    addFilesAndSelectFirstNew (AudioFileCollector::collect (files), true);
}

void MainComponent::addFilesAndSelectFirstNew (const juce::Array<juce::File>& files, bool startOnlyIfIdle)
{
    if (files.isEmpty())
        return;

    const int  firstNewIndex = playlistManager->getItems().size();
    const bool wasPlaying    = audioEngine->isPlaying();

    playlistManager->addFiles (files);

    if (! startOnlyIfIdle || ! wasPlaying)
        playlistManager->selectItem (firstNewIndex);
}

// ==============================================================================
// Export dialogs, forwarded from the transport's record menu
// ==============================================================================

void MainComponent::showExportVideoDialog()
{
    videoExport->showExportDialog();
}

void MainComponent::showExportPlaylistVideoDialog()
{
    playlistVideoExport->showExportDialog();
}

// ==============================================================================
// Component overrides
// ==============================================================================

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkgrey);
}

void MainComponent::resized()
{
    if (! panelsAreReady())
        return;

    const auto state = currentPanelState();
    const auto areas = MainLayout::computeAreas (getLocalBounds(), state);

    playerControls->setBounds (areas.playerControls);
    mainToolbar->setBounds (areas.toolbar);

    if (! state.analysisCollapsed)
        analysisUI->setBounds (areas.analysis);

    // While it is reparented into the fullscreen host window (see
    // FullScreenWindowController::openPatternViewer), the pattern viewer's usual
    // slot is still reserved above so the panels below don't shift, but its
    // fullscreen bounds are not stomped on.
    if (state.patternViewerVisible && patternViewer->getParentComponent() == this)
        patternViewer->setBounds (areas.patternViewer);

    // Video and the spectrum visualizer share this slot (see
    // updatePanelsForCurrentTrack(), which keeps them mutually exclusive). Same
    // fullscreen reasoning as the pattern viewer: reserve the slot, don't
    // overwrite the hosted bounds.
    visualization->setBounds (areas.visualization);
    if (videoPreview->getParentComponent() == this)
        videoPreview->setBounds (areas.videoPreview);

    // FX chain panel + on-screen keyboard for playing loaded synths.
    if (! state.fxCollapsed)
    {
        pluginChainUI->setBounds (areas.pluginChain);
        midiInputBtn->setBounds (areas.midiButton);
        midiKeyboard->setBounds (areas.midiKeyboard);
    }

    if (! state.playlistCollapsed)
        playlistEditor->setBounds (areas.playlist);

    if (! state.eqCollapsed)
        equalizerUI->setBounds (areas.equalizer);
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component* /*originator*/)
{
    if (visualization->handleDigitHotkey (key))
        return true;

    return handleTransportKeyPress (key, audioEngine.get(), playlistManager.get());
}

void MainComponent::timerCallback()
{
    if (patternViewer->isVisible())
    {
        patternViewer->setPlaybackPosition (audioEngine->getPatternPlayPosition(),
                                            audioEngine->isPlaying());
    }

    // Both exporters poll rather than call back -- see their controllers for why.
    videoExport->pollProgress();
    playlistVideoExport->pollProgress();
}
