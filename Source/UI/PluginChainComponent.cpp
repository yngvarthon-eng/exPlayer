#include "PluginChainComponent.h"
#include "../Plugins/PluginChainPersistence.h"
#include "../Audio/AudioEngine.h"

namespace
{
    // DocumentWindow::closeButtonPressed() is a no-op by default (JUCE requires an
    // override to actually dismiss the window) — this gives every editor/parameter
    // window opened below a working close button via a plain callback.
    class ClosableEditorWindow : public juce::DocumentWindow
    {
    public:
        using juce::DocumentWindow::DocumentWindow;
        std::function<void()> onClose;
        void closeButtonPressed() override { if (onClose) onClose(); }
    };
}

// ---------------------------------------------------------------------------
// Per-row component: plugin name + Params + bypass toggle
// ---------------------------------------------------------------------------
class PluginRowComponent : public juce::Component
{
public:
    std::function<void(bool)> onBypassChanged;
    std::function<void(bool)> onSidechainOnlyChanged;
    std::function<void()>     onParamsClicked;

    PluginRowComponent()
    {
        // This component fills the whole row (see resized()) and JUCE dispatches a
        // click to the deepest component under the cursor — without this, a click
        // on the name-label area is silently swallowed here (Label intercepts
        // clicks by default even though it does nothing with them) and never
        // reaches ListBox's own row-selection handling, so nothing ever gets
        // selected and Remove/Up/Down (which all require a selected row) look dead.
        // (false, true) = this component itself doesn't want clicks (so they fall
        // through to the list for selection), but children — the actual buttons —
        // still do.
        setInterceptsMouseClicks(false, true);

        paramsButton.setButtonText("Params");
        paramsButton.onClick = [this] { if (onParamsClicked) onParamsClicked(); };

        bypassButton.setButtonText("Bypass");
        bypassButton.setClickingTogglesState(true);
        bypassButton.setToggleState(false, juce::dontSendNotification);
        bypassButton.onClick = [this] {
            if (onBypassChanged)
                onBypassChanged(bypassButton.getToggleState());
        };

        sidechainOnlyButton.setButtonText("SC Only");
        sidechainOnlyButton.setClickingTogglesState(true);
        sidechainOnlyButton.setToggleState(false, juce::dontSendNotification);
        sidechainOnlyButton.setTooltip(
            "Feed only the next sidechain-capable plugin's carrier input (e.g. a "
            "synth driving a vocoder) instead of the main signal path — your track "
            "audio passes through this plugin untouched.");
        sidechainOnlyButton.onClick = [this] {
            if (onSidechainOnlyChanged)
                onSidechainOnlyChanged(sidechainOnlyButton.getToggleState());
        };

        addAndMakeVisible(paramsButton);
        addAndMakeVisible(bypassButton);
        addAndMakeVisible(sidechainOnlyButton);
        addAndMakeVisible(nameLabel);
        nameLabel.setJustificationType(juce::Justification::centredLeft);
        nameLabel.setInterceptsMouseClicks(false, false); // same reasoning as above
    }

    void setPluginName(const juce::String& name)   { nameLabel.setText(name, juce::dontSendNotification); }
    void setBypassed(bool bypassed)
    {
        bypassButton.setToggleState(bypassed, juce::dontSendNotification);
        nameLabel.setEnabled(!bypassed);
    }
    void setSidechainOnly(bool sidechainOnly)
    {
        sidechainOnlyButton.setToggleState(sidechainOnly, juce::dontSendNotification);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(2);
        bypassButton.setBounds(bounds.removeFromRight(70));
        bounds.removeFromRight(4);
        sidechainOnlyButton.setBounds(bounds.removeFromRight(70));
        bounds.removeFromRight(4);
        paramsButton.setBounds(bounds.removeFromRight(60));
        bounds.removeFromRight(4);
        nameLabel.setBounds(bounds);
    }

private:
    juce::Label      nameLabel;
    juce::TextButton sidechainOnlyButton;
    juce::TextButton paramsButton;
    juce::TextButton bypassButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginRowComponent)
};

// ---------------------------------------------------------------------------
// PluginChainComponent
// ---------------------------------------------------------------------------
PluginChainComponent::PluginChainComponent(PluginHostManager* hostMgr, PluginChain* pluginChain, AudioEngine& engine)
    : hostManager(hostMgr), chain(pluginChain), audioEngine(engine)
{
    pluginList.setModel(this);
    pluginList.setRowHeight(24);
    addAndMakeVisible(pluginList);

    addBtn     = std::make_unique<juce::TextButton>("Add");
    removeBtn  = std::make_unique<juce::TextButton>("Remove");
    moveUpBtn  = std::make_unique<juce::TextButton>("Up");
    moveDownBtn= std::make_unique<juce::TextButton>("Down");
    scanBtn    = std::make_unique<juce::TextButton>("Scan");

    addBtn->onClick      = [this] { onAddClicked(); };
    removeBtn->onClick   = [this] { onRemoveClicked(); };
    moveUpBtn->onClick   = [this] { onMoveUpClicked(); };
    moveDownBtn->onClick = [this] { onMoveDownClicked(); };
    scanBtn->onClick     = [this] { onScanClicked(); };

    addAndMakeVisible(*addBtn);
    addAndMakeVisible(*removeBtn);
    addAndMakeVisible(*moveUpBtn);
    addAndMakeVisible(*moveDownBtn);
    addAndMakeVisible(*scanBtn);

    presetCombo = std::make_unique<juce::ComboBox>();
    presetCombo->onChange = [this] { onPresetSelected(); };
    savePresetBtn = std::make_unique<juce::TextButton>("Save");
    savePresetBtn->onClick = [this] { onSavePreset(); };
    deletePresetBtn = std::make_unique<juce::TextButton>("Delete");
    deletePresetBtn->onClick = [this] { onDeletePreset(); };

    addAndMakeVisible(*presetCombo);
    addAndMakeVisible(*savePresetBtn);
    addAndMakeVisible(*deletePresetBtn);
    refreshPresetCombo();

    renderBtn = std::make_unique<juce::TextButton>("Render to WAV");
    renderBtn->onClick = [this] { onRenderClicked(); };
    addAndMakeVisible(*renderBtn);

    renderStatusLabel = std::make_unique<juce::Label>("RenderStatus");
    renderStatusLabel->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    renderStatusLabel->setFont(juce::Font(12.0f));
    addAndMakeVisible(*renderStatusLabel);

    updateButtonStates();
}

PluginChainComponent::~PluginChainComponent()
{
    pluginList.setModel(nullptr);
    openEditors.clear();
    if (isRendering)
        finishRender();
}

void PluginChainComponent::refreshFromChain()
{
    pluginList.updateContent();
    updateButtonStates();
}

void PluginChainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds(), 1);

    g.setColour(juce::Colours::lightgrey);
    g.setFont(12.0f);
    g.drawText("FX Chain", getLocalBounds().removeFromTop(18).reduced(4, 0),
               juce::Justification::centredLeft);
}

void PluginChainComponent::resized()
{
    auto area = getLocalBounds().reduced(2);
    area.removeFromTop(18); // title

    // Preset row at bottom
    auto presetRow = area.removeFromBottom(24);
    deletePresetBtn->setBounds(presetRow.removeFromRight(60));
    presetRow.removeFromRight(2);
    savePresetBtn->setBounds(presetRow.removeFromRight(50));
    presetRow.removeFromRight(4);
    presetCombo->setBounds(presetRow);
    area.removeFromBottom(2);

    // Button row above it
    auto btnRow = area.removeFromBottom(24);
    const int btnW = 60;
    addBtn->setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(2);
    removeBtn->setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(2);
    moveUpBtn->setBounds(btnRow.removeFromLeft(36));
    btnRow.removeFromLeft(2);
    moveDownBtn->setBounds(btnRow.removeFromLeft(48)); // "Down" needs more than "Up" or it ellipsizes to "..."
    btnRow.removeFromLeft(8);
    scanBtn->setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(8);
    renderBtn->setBounds(btnRow.removeFromLeft(120));
    btnRow.removeFromLeft(8);
    renderStatusLabel->setBounds(btnRow);

    area.removeFromBottom(2);

    pluginList.setBounds(area);
}

// ---------------------------------------------------------------------------
// ListBoxModel
// ---------------------------------------------------------------------------
int PluginChainComponent::getNumRows()
{
    return chain->size();
}

void PluginChainComponent::paintListBoxItem(int, juce::Graphics&, int, int, bool)
{
    // Painting is handled by the row component (refreshComponentForRow)
}

juce::Component* PluginChainComponent::refreshComponentForRow(
    int rowNumber, bool /*isRowSelected*/, juce::Component* existingComponentToUpdate)
{
    auto* row = dynamic_cast<PluginRowComponent*>(existingComponentToUpdate);
    if (row == nullptr)
        row = new PluginRowComponent();

    if (rowNumber < chain->size())
    {
        row->setPluginName(chain->getPluginName(rowNumber));
        row->setBypassed(chain->isBypassed(rowNumber));
        row->setSidechainOnly(chain->isSidechainOnly(rowNumber));
        // Re-bind index (lambdas capture by value at creation; update via setter)
        row->onBypassChanged = [this, rowNumber](bool bypassed)
        {
            chain->setBypass(rowNumber, bypassed);
            persistChain();
        };
        row->onSidechainOnlyChanged = [this, rowNumber](bool sidechainOnly)
        {
            chain->setSidechainOnly(rowNumber, sidechainOnly);
            persistChain();
        };
        row->onParamsClicked = [this, rowNumber] { openParametersForRow(rowNumber); };
    }
    return row;
}

void PluginChainComponent::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    openEditorForRow(row);
}

void PluginChainComponent::selectedRowsChanged(int)
{
    updateButtonStates();
}

void PluginChainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    pluginList.updateContent();
    updateButtonStates();
}

void PluginChainComponent::timerCallback()
{
    if (!isRendering)
        return;

    const double duration = audioEngine.getTotalDuration();
    const double position = audioEngine.getCurrentPlaybackTime();
    const bool reachedEnd = duration > 0.0 && position >= duration - 0.05;

    if (reachedEnd || !audioEngine.isPlaying())
    {
        finishRender();
        return;
    }

    renderStatusLabel->setText(
        "Rendering " + juce::String((int)position) + "s / " + juce::String((int)duration) + "s",
        juce::dontSendNotification);
}

// ---------------------------------------------------------------------------
// Button handlers
// ---------------------------------------------------------------------------
void PluginChainComponent::onAddClicked()
{
    const auto& types = hostManager->getKnownPlugins().getTypes();

    if (types.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No plugins found",
            "Click Scan to search for installed plugins first.");
        return;
    }

    juce::PopupMenu menu;

    // Group by format
    juce::StringArray formats;
    for (const auto& t : types)
        formats.addIfNotAlreadyThere(t.pluginFormatName);
    formats.sort(false);

    // Item IDs must be unique across the *whole* PopupMenu (they're per-menu, not
    // per-submenu) since the result callback below maps a single ID straight back
    // to an index in `types` — so each item's ID is that same global index + 1,
    // not a per-submenu counter that restarts at 1 for every format.
    for (const auto& fmt : formats)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < types.size(); ++i)
            if (types[i].pluginFormatName == fmt)
                sub.addItem(i + 1, types[i].name + " (" + types[i].manufacturerName + ")");
        menu.addSubMenu(fmt, sub);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addBtn.get()),
        [this, types](int result)
        {
            if (result <= 0)
                return;

            // Map result back to PluginDescription
            int idx = result - 1;
            if (idx < types.size())
                loadPluginFromDescription(types[idx]);
        });
}

void PluginChainComponent::onRemoveClicked()
{
    int row = pluginList.getSelectedRow();
    if (row >= 0)
    {
        chain->removePlugin(row);
        pluginList.updateContent();
        updateButtonStates();
        persistChain();
    }
}

void PluginChainComponent::onMoveUpClicked()
{
    int row = pluginList.getSelectedRow();
    if (row > 0)
    {
        chain->movePlugin(row, row - 1);
        pluginList.selectRow(row - 1);
        pluginList.updateContent();
        persistChain();
    }
}

void PluginChainComponent::onMoveDownClicked()
{
    int row = pluginList.getSelectedRow();
    if (row >= 0 && row < chain->size() - 1)
    {
        chain->movePlugin(row, row + 1);
        pluginList.selectRow(row + 1);
        pluginList.updateContent();
        persistChain();
    }
}

void PluginChainComponent::onScanClicked()
{
    scanBtn->setEnabled(false);
    scanBtn->setButtonText("Scanning...");

    juce::Thread::launch([this]
    {
        hostManager->scanDefaultDirectories([](int found, int total)
        {
            juce::ignoreUnused(found, total);
        });

        juce::MessageManager::callAsync([this]
        {
            scanBtn->setEnabled(true);
            scanBtn->setButtonText("Scan");
            pluginList.updateContent();

            int count = hostManager->getKnownPlugins().getNumTypes();
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Scan complete",
                "Found " + juce::String(count) + " plugin(s).");
        });
    });
}

void PluginChainComponent::loadPluginFromDescription(const juce::PluginDescription& desc)
{
    juce::String error;
    // Use current audio device settings — 44100/512 as fallback
    auto instance = hostManager->loadPlugin(desc, 44100.0, 512, error);

    if (instance == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Failed to load plugin",
            error);
        return;
    }

    chain->addPlugin(std::move(instance));
    pluginList.updateContent();
    updateButtonStates();
    persistChain();
}

void PluginChainComponent::openEditorForRow(int row)
{
    auto* plugin = chain->getPlugin(row);
    if (plugin == nullptr || !plugin->hasEditor())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No editor",
            "This plugin has no graphical editor.");
        return;
    }

    auto* editor = plugin->createEditorIfNeeded();
    if (editor == nullptr)
        return;

    auto* window = new ClosableEditorWindow(
        plugin->getName(),
        juce::Colours::darkgrey,
        juce::DocumentWindow::closeButton);

    window->setContentOwned(editor, true);
    window->setResizable(true, false);
    window->setUsingNativeTitleBar(true);
    window->centreWithSize(editor->getWidth(), editor->getHeight());
    window->onClose = [this, window] { openEditors.removeObject(window); };
    window->setVisible(true);

    openEditors.add(window);
}

void PluginChainComponent::openParametersForRow(int row)
{
    auto* plugin = chain->getPlugin(row);
    if (plugin == nullptr)
        return;

    // Always available regardless of hasEditor()/createEditor() — the only way to
    // control an LV2 plugin (never has a native editor) or a CLAP plugin (its
    // native editor is currently a stub even when hasEditor() reports true).
    auto* editor = new juce::GenericAudioProcessorEditor(*plugin);

    auto* window = new ClosableEditorWindow(
        plugin->getName() + " Parameters",
        juce::Colours::darkgrey,
        juce::DocumentWindow::closeButton);

    window->setContentOwned(editor, true);
    window->setResizable(true, false);
    window->setUsingNativeTitleBar(true);
    window->centreWithSize(editor->getWidth(), editor->getHeight());
    window->onClose = [this, window]
    {
        // Chain mutations autosave immediately, but parameter tweaks made in this
        // panel don't hook any single "changed" event — capture the chain's state
        // once more on close so they're not lost on the next restart.
        persistChain();
        openEditors.removeObject(window);
    };
    window->setVisible(true);

    openEditors.add(window);
}

void PluginChainComponent::onRenderClicked()
{
    if (isRendering)
    {
        finishRender();
        return;
    }

    if (audioEngine.isCurrentlyStreaming())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Render to WAV",
            "Can't render a live stream — load a local track first.");
        return;
    }

    if (audioEngine.getTotalDuration() <= 0.0)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Render to WAV",
            "Load a track first.");
        return;
    }

    renderFileChooser = std::make_unique<juce::FileChooser>(
        "Render chain output to WAV...",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("chain_render.wav"),
        "*.wav");
    renderFileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;
            startRender(file);
        });
}

void PluginChainComponent::startRender(const juce::File& outputFile)
{
    // Plays the track from the top while diverting AudioEngine's own recorder
    // (same code path as the toolbar's "Record raw audio") to outputFile, so the
    // render is captured post-EQ/post-chain/post-gain from the real audio
    // callback — not a re-instantiated offline copy of the hosted plugins,
    // which wouldn't be safe to build for VST/LV2/CLAP instances.
    audioEngine.setCurrentPosition(0.0);
    audioEngine.startRecording(outputFile);
    audioEngine.play();

    isRendering = true;
    renderBtn->setButtonText("Stop Render");
    renderStatusLabel->setText("Rendering...", juce::dontSendNotification);
    startTimer(100);
}

void PluginChainComponent::finishRender()
{
    stopTimer();
    audioEngine.stopRecording();
    audioEngine.stop();

    isRendering = false;
    renderBtn->setButtonText("Render to WAV");
    renderStatusLabel->setText({}, juce::dontSendNotification);
}

void PluginChainComponent::updateButtonStates()
{
    const int row   = pluginList.getSelectedRow();
    const int count = chain->size();

    removeBtn->setEnabled(row >= 0);
    moveUpBtn->setEnabled(row > 0);
    moveDownBtn->setEnabled(row >= 0 && row < count - 1);
}

void PluginChainComponent::persistChain()
{
    PluginChainPersistence::save(*chain);
}

void PluginChainComponent::refreshPresetCombo()
{
    presetCombo->clear(juce::dontSendNotification);
    int id = 1;
    for (const auto& preset : presetManager.getPresets())
        presetCombo->addItem(preset.name, id++);
    presetCombo->setText({}, juce::dontSendNotification);
}

void PluginChainComponent::onPresetSelected()
{
    const auto& presets = presetManager.getPresets();
    int idx = presetCombo->getSelectedItemIndex();
    if (!juce::isPositiveAndBelow(idx, presets.size()))
        return;

    presetManager.applyPreset(presets[(size_t)idx].name, *chain, *hostManager);
    pluginList.updateContent();
    updateButtonStates();
    persistChain();
}

void PluginChainComponent::onSavePreset()
{
    auto* w = new juce::AlertWindow("Save FX Chain Preset",
                                    "Enter a name for this preset:",
                                    juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", presetCombo->getText(), "Preset name:");
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    w->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, w](int result)
        {
            if (result == 1)
            {
                auto name = w->getTextEditorContents("name").trim();
                if (name.isNotEmpty())
                {
                    presetManager.savePreset(name, *chain);
                    refreshPresetCombo();

                    const auto& presets = presetManager.getPresets();
                    for (int i = 0; i < (int)presets.size(); ++i)
                        if (presets[(size_t)i].name == name)
                            presetCombo->setSelectedItemIndex(i, juce::dontSendNotification);
                }
            }
        }), true);
}

void PluginChainComponent::onDeletePreset()
{
    const auto& presets = presetManager.getPresets();
    int idx = presetCombo->getSelectedItemIndex();
    if (!juce::isPositiveAndBelow(idx, presets.size()))
        return;

    presetManager.deletePreset(presets[(size_t)idx].name);
    refreshPresetCombo();
}
