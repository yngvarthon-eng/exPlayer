#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Plugins/PluginChain.h"
#include "../Plugins/PluginHostManager.h"
#include "../Plugins/PluginChainPresetManager.h"

class AudioEngine;

/**
 * UI panel showing the plugin effect chain.
 *
 * Provides:
 *   - A list of loaded plugins with per-row bypass toggles and a Params button
 *     (opens a generic parameter editor — juce::GenericAudioProcessorEditor —
 *     since not every hosted format has a usable native editor: LV2 never does,
 *     and CLAP's native editor is currently a stub even when it reports one)
 *   - Add (from scanned list or file browser), Remove, Move Up/Down buttons
 *   - Scan button to find plugins in default directories
 *   - Double-click to open the plugin's native editor window
 *   - Named chain presets (save/load/delete) and autosave of the current chain
 *     across app restarts (see PluginChainPersistence)
 *   - Render to WAV: bounces the current track through this chain (plus the
 *     live EQ/gain/bit-depth stages) by driving normal playback from the top
 *     while diverting AudioEngine's existing recorder to the chosen file, so
 *     the render always reflects the real, currently-loaded plugin instances.
 */
class PluginChainComponent : public juce::Component,
                             private juce::ListBoxModel,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    PluginChainComponent(PluginHostManager* hostManager, PluginChain* chain, AudioEngine& engine);
    ~PluginChainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Call after the chain was changed from outside this component (e.g. restoring
    // the persisted chain at startup) so the list reflects the new contents.
    void refreshFromChain();

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g,
                          int width, int height,
                          bool rowIsSelected) override;
    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected,
                                            juce::Component* existingComponentToUpdate) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;

    // ChangeListener — refreshes list when chain changes externally
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // Timer — polls playback position while a render is in progress so it can
    // stop the recording automatically once the track reaches its end.
    void timerCallback() override;

    void onAddClicked();
    void onRemoveClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onScanClicked();
    void loadPluginFromDescription(const juce::PluginDescription& desc);
    void openEditorForRow(int row);
    void openParametersForRow(int row);
    void updateButtonStates();

    // Saves the current chain to disk (fx_chain.xml) — called after every mutation
    // (add/remove/move/bypass/preset-apply) so the chain survives a restart even
    // without an explicit "save" action from the user.
    void persistChain();

    void refreshPresetCombo();
    void onSavePreset();
    void onDeletePreset();
    void onPresetSelected();

    // Render-to-WAV: starts/stops itself via AudioEngine's existing recorder.
    void onRenderClicked();
    void startRender(const juce::File& outputFile);
    void finishRender();

    PluginHostManager* hostManager;
    PluginChain*       chain;
    AudioEngine&       audioEngine;
    PluginChainPresetManager presetManager;

    juce::ListBox              pluginList;
    std::unique_ptr<juce::TextButton> addBtn, removeBtn, moveUpBtn, moveDownBtn, scanBtn;
    std::unique_ptr<juce::ComboBox>   presetCombo;
    std::unique_ptr<juce::TextButton> savePresetBtn, deletePresetBtn;

    std::unique_ptr<juce::TextButton> renderBtn;
    std::unique_ptr<juce::Label>      renderStatusLabel;
    std::unique_ptr<juce::FileChooser> renderFileChooser;
    bool isRendering = false;

    // Open editor / parameter windows — kept alive until user closes them
    juce::OwnedArray<juce::DocumentWindow> openEditors;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChainComponent)
};
