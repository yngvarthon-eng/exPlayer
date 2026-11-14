#include "MicRecorderComponent.h"
#include "../Util/AppDataDirectory.h"

namespace
{
    constexpr int refreshHz = 30;
    constexpr int rowHeight = 28;
    constexpr int gap = 6;

    const juce::Colour panelColour  { 0xff26292e };
    const juce::Colour recordColour { 0xff8b0000 };
}

// ==============================================================================
// Input level meter: one horizontal bar per channel, -60..0 dBFS, with a
// slowly falling peak so short transients stay readable.
// ==============================================================================

class MicRecorderComponent::LevelMeter : public juce::Component
{
public:
    void push(int channel, float peak)
    {
        if (channel >= 2)
            return;
        levels[channel] = juce::jmax(peak, levels[channel] * 0.85f);
        holds[channel] = peak >= holds[channel] ? peak : holds[channel] * 0.97f;
        clipped[channel] = clipped[channel] > 0 ? clipped[channel] - 1 : 0;
        if (peak >= 0.999f)
            clipped[channel] = refreshHz; // keep the clip light on for a second
        repaint();
    }

    void setNumChannels(int n) { numChannels = juce::jlimit(1, 2, n); repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(area, 3.0f);
        area.reduce(2.0f, 2.0f);

        const float barHeight = (area.getHeight() - (float) (numChannels - 1)) / (float) numChannels;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto bar = area.removeFromTop(barHeight);
            area.removeFromTop(1.0f);

            auto toWidth = [&bar](float level)
            {
                const float db = juce::Decibels::gainToDecibels(level, -60.0f);
                return bar.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
            };

            const float width = toWidth(levels[ch]);
            juce::ColourGradient gradient(juce::Colour(0xff43a047), bar.getX(), 0.0f,
                                          juce::Colour(0xffe53935), bar.getRight(), 0.0f, false);
            gradient.addColour(0.8, juce::Colour(0xfffdd835)); // about -12 dB
            g.setGradientFill(gradient);
            g.fillRect(bar.withWidth(width));

            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.fillRect(bar.getX() + toWidth(holds[ch]) - 1.0f, bar.getY(), 2.0f, bar.getHeight());

            if (clipped[ch] > 0)
            {
                g.setColour(juce::Colours::red);
                g.fillRect(bar.removeFromRight(6.0f));
            }
        }
    }

private:
    int numChannels = 2;
    float levels[2] {};
    float holds[2] {};
    int clipped[2] {};
};

// ==============================================================================
// Construction
// ==============================================================================

MicRecorderComponent::MicRecorderComponent(AudioEngine& engine)
    : audioEngine(engine)
{
    levelMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible(*levelMeter);

    inputLabel.setFont(juce::Font(13.0f));
    addAndMakeVisible(inputLabel);
    addAndMakeVisible(inputDeviceButton);
    inputDeviceButton.onClick = [this] { showInputDeviceDialog(); };

    // Item ids are ChannelMode + 1.
    channelModeBox.addItem("Auto mono", 1);
    channelModeBox.addItem("Left only", 2);
    channelModeBox.addItem("Right only", 3);
    channelModeBox.addItem("Stereo", 4);
    channelModeBox.setTooltip("Most mics are mono and arrive on only one side of a stereo jack. "
                              "Auto mono records just the side that has signal.");
    channelModeBox.setWantsKeyboardFocus(false);
    loadChannelMode();
    channelModeBox.setSelectedId((int) recorder.getChannelMode() + 1, juce::dontSendNotification);
    channelModeBox.onChange = [this]
    {
        recorder.setChannelMode((MicRecorder::ChannelMode) (channelModeBox.getSelectedId() - 1));
        saveChannelMode();
        refreshInputLabel();
    };
    addAndMakeVisible(channelModeBox);

    recordButton.setColour(juce::TextButton::buttonColourId, recordColour);
    recordButton.onClick = [this] { toggleRecording(); };
    playButton.onClick   = [this] { togglePlayback(); };
    stopButton.onClick   = [this] { stopAll(); };

    for (auto* label : { &timeLabel, &selectionLabel })
    {
        label->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
        label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(*label);
    }

    undoButton.onClick      = [this] { undo(); };
    redoButton.onClick      = [this] { redo(); };
    trimButton.onClick      = [this] { trimToSelection(); };
    deleteButton.onClick    = [this] { deleteSelection(); };
    silenceButton.onClick   = [this] { applyEdit([this] { return clip.silence(waveform.getSelection()); }); };
    fadeInButton.onClick    = [this] { applyEdit([this] { return clip.fadeIn(waveform.getSelection()); }); };
    fadeOutButton.onClick   = [this] { applyEdit([this] { return clip.fadeOut(waveform.getSelection()); }); };
    normalizeButton.onClick = [this] { applyEdit([this] { return clip.normalize(waveform.getSelection()); }); };

    zoomInButton.onClick  = [this] { waveform.zoomIn(); };
    zoomOutButton.onClick = [this] { waveform.zoomOut(); };
    zoomFitButton.onClick = [this] { waveform.zoomToFit(); };
    zoomSelButton.onClick = [this] { waveform.zoomToSelection(); };

    makeMonoButton.onClick     = [this] { showMakeMonoMenu(); };
    removeHumButton.onClick    = [this] { removeHum(); };
    noiseProfileButton.onClick = [this] { learnNoiseProfile(); };
    reduceNoiseButton.onClick  = [this] { showReduceNoiseMenu(); };

    newButton.onClick  = [this] { newClip(); };
    saveButton.onClick = [this] { saveClip(); };
    addToPlaylistToggle.setToggleState(true, juce::dontSendNotification);

    recordButton.setTooltip("Record from the microphone (R). A new take is added to the end of the clip.");
    playButton.setTooltip("Play the selection, or from the cursor (Space)");
    stopButton.setTooltip("Stop recording/playback (Space)");
    trimButton.setTooltip("Keep only the selection (Ctrl+T)");
    deleteButton.setTooltip("Delete the selection (Del)");
    silenceButton.setTooltip("Replace the selection with silence");
    fadeInButton.setTooltip("Fade in across the selection");
    fadeOutButton.setTooltip("Fade out across the selection");
    normalizeButton.setTooltip("Raise the selection (or whole clip) so its peak is at -1 dB");
    makeMonoButton.setTooltip("Turn a stereo take into mono, e.g. keep only the channel the mic is on");
    removeHumButton.setTooltip("Filter out mains hum (50/60 Hz and harmonics) and low rumble "
                               "from the selection, or the whole clip");
    noiseProfileButton.setTooltip("Teach Reduce Noise what the background noise sounds like: from the "
                                  "selection (drag over a quiet part), or the quietest part if nothing is selected");
    reduceNoiseButton.setTooltip("Turn down background hiss in the selection (or whole clip). Uses the "
                                 "noise profile if you made one, otherwise the quietest parts of the take");

    for (auto* button : std::initializer_list<juce::Button*> {
             &inputDeviceButton, &recordButton, &playButton, &stopButton,
             &undoButton, &redoButton, &trimButton, &deleteButton, &silenceButton,
             &fadeInButton, &fadeOutButton, &normalizeButton,
             &makeMonoButton, &removeHumButton, &noiseProfileButton, &reduceNoiseButton,
             &zoomInButton, &zoomOutButton, &zoomFitButton, &zoomSelButton,
             &newButton, &saveButton, &addToPlaylistToggle })
    {
        // Leave keyboard focus on this component so the shortcuts keep working.
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }

    hintText = "Click = cursor   drag = select   wheel = scroll   Ctrl+wheel = zoom   "
               "R record   Space play/stop   Del delete   Ctrl+Z undo   Ctrl+S save";
    hintLabel.setText(hintText, juce::dontSendNotification);
    hintLabel.setFont(juce::Font(12.0f));
    hintLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(hintLabel);

    waveform.onSelectionChanged = [this] { updateButtons(); updateTimeLabels(); };
    addAndMakeVisible(waveform);

    lastSaveDirectory = juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    inputError = recorder.openInputDevice();
    refreshInputLabel();

    audioEngine.setAuxiliarySource(&playback);

    setWantsKeyboardFocus(true);
    setSize(1180, 560);
    updateButtons();
    updateTimeLabels();
    startTimerHz(refreshHz);
}

MicRecorderComponent::~MicRecorderComponent()
{
    stopTimer();
    audioEngine.setAuxiliarySource(nullptr); // before playback/clip go away
    recorder.stopRecording();
}

// ==============================================================================
// Layout
// ==============================================================================

void MicRecorderComponent::paint(juce::Graphics& g)
{
    g.fillAll(panelColour);
}

void MicRecorderComponent::resized()
{
    auto area = getLocalBounds().reduced(gap * 2);

    auto placeRow = [](juce::Rectangle<int>& row, std::initializer_list<std::pair<juce::Component*, int>> items)
    {
        for (auto& [component, width] : items)
        {
            component->setBounds(row.removeFromLeft(width));
            row.removeFromLeft(gap);
        }
    };

    // Input device row.
    auto inputRow = area.removeFromTop(rowHeight);
    inputDeviceButton.setBounds(inputRow.removeFromRight(120));
    inputRow.removeFromRight(gap);
    levelMeter->setBounds(inputRow.removeFromRight(200).reduced(0, 4));
    inputRow.removeFromRight(gap);
    channelModeBox.setBounds(inputRow.removeFromRight(110));
    inputRow.removeFromRight(gap);
    inputLabel.setBounds(inputRow);
    area.removeFromTop(gap);

    // Transport row.
    auto transportRow = area.removeFromTop(rowHeight + 4);
    placeRow(transportRow, { { &recordButton, 90 }, { &playButton, 80 }, { &stopButton, 80 } });
    transportRow.removeFromLeft(gap * 2);
    timeLabel.setBounds(transportRow.removeFromLeft(240));
    selectionLabel.setBounds(transportRow);
    area.removeFromTop(gap);

    hintLabel.setBounds(area.removeFromBottom(18));
    area.removeFromBottom(gap);

    // Two rows of tools: editing + clean-up, then view + file.
    auto editArea = area.removeFromBottom(rowHeight * 2 + gap);
    auto editRow = editArea.removeFromTop(rowHeight);
    placeRow(editRow, { { &undoButton, 56 }, { &redoButton, 56 } });
    editRow.removeFromLeft(gap * 2);
    placeRow(editRow, { { &trimButton, 56 }, { &deleteButton, 60 }, { &silenceButton, 64 },
                        { &fadeInButton, 64 }, { &fadeOutButton, 70 }, { &normalizeButton, 80 } });
    editRow.removeFromLeft(gap * 2);
    placeRow(editRow, { { &makeMonoButton, 84 }, { &removeHumButton, 92 },
                        { &noiseProfileButton, 100 }, { &reduceNoiseButton, 104 } });

    auto secondRow = editArea.withTrimmedTop(gap);
    placeRow(secondRow, { { &zoomInButton, 60 }, { &zoomOutButton, 60 }, { &zoomFitButton, 44 },
                          { &zoomSelButton, 72 } });
    secondRow.removeFromLeft(gap * 2);
    placeRow(secondRow, { { &newButton, 50 }, { &saveButton, 70 }, { &addToPlaylistToggle, 130 } });
    area.removeFromBottom(gap);

    waveform.setBounds(area);
}

// ==============================================================================
// Recording and playback
// ==============================================================================

void MicRecorderComponent::toggleRecording()
{
    if (recorder.isRecording())
        stopRecording();
    else
        startRecording();
}

void MicRecorderComponent::startRecording()
{
    playback.stop();

    if (! clip.isEmpty() && std::abs(clip.getSampleRate() - recorder.getInputSampleRate()) > 0.5)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Record",
            "This clip was recorded at " + juce::String(clip.getSampleRate(), 0)
                + " Hz but the input now runs at " + juce::String(recorder.getInputSampleRate(), 0)
                + " Hz.\n\nSave it and press New to start a fresh clip, or switch the input back.");
        return;
    }

    juce::String error;
    if (! recorder.startRecording(error))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Record", error);
        return;
    }
    // Nothing is drained into the clip before the next timer tick, so this
    // snapshot is taken before the take and undoes it in one step.
    clip.pushUndoState();

    dirty = true;
    waveform.setSelection({});
    waveform.setFollowRecording(true);
    updateButtons();
}

void MicRecorderComponent::stopRecording()
{
    if (! recorder.isRecording())
        return;

    recorder.stopRecording();
    recorder.drainInto(clip);
    waveform.setFollowRecording(false);
    waveform.clipChanged();
    waveform.zoomToFit();
    updateButtons();
    updateTimeLabels();

    if (const auto dropped = recorder.getDroppedSamples(); dropped > 0)
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Record",
            juce::String(dropped) + " samples were lost because the app was too busy. "
            "The take may contain small gaps.");
}

void MicRecorderComponent::togglePlayback()
{
    if (recorder.isRecording())
        stopRecording();
    else if (playback.isPlaying())
        playback.stop();
    else
        startPlayback();
    updateButtons();
}

void MicRecorderComponent::startPlayback()
{
    if (clip.isEmpty() || recorder.isRecording())
        return;

    const auto selection = waveform.getSelection();
    if (! selection.isEmpty())
    {
        playback.start(selection.getStart(), selection.getEnd());
    }
    else
    {
        auto from = waveform.getCursor();
        if (from >= clip.getNumSamples())
            from = 0; // cursor parked at the end: play from the top
        playback.start(from, clip.getNumSamples());
    }
    updateButtons();
}

void MicRecorderComponent::stopAll()
{
    stopRecording();
    playback.stop();
    updateButtons();
}

void MicRecorderComponent::timerCallback()
{
    // Always drain: also picks up the last few blocks after a stop.
    const bool grew = recorder.drainInto(clip) > 0;
    if (grew)
        waveform.clipChanged();

    const int channels = juce::jmax(1, recorder.getNumInputChannels());
    levelMeter->setNumChannels(channels);
    for (int ch = 0; ch < channels; ++ch)
        levelMeter->push(ch, recorder.takePeak(ch));

    waveform.setPlayhead(playback.isPlaying() ? playback.getPositionSamples()
                                              : (recorder.isRecording() ? (double) clip.getNumSamples() : -1.0));

    if (statusUntilMs != 0 && juce::Time::getMillisecondCounter() > statusUntilMs)
    {
        statusUntilMs = 0;
        hintLabel.setText(hintText, juce::dontSendNotification);
        hintLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    }

    updateButtons(); // e.g. playback ran off the end of its range
    updateTimeLabels();
}

// ==============================================================================
// Editing
// ==============================================================================

void MicRecorderComponent::applyEdit(const std::function<bool()>& edit)
{
    if (recorder.isRecording() || clip.isEmpty())
        return;

    playback.stop();
    if (edit())
    {
        dirty = true;
        waveform.clipChanged();
    }
    updateButtons();
    updateTimeLabels();
}

void MicRecorderComponent::deleteSelection()
{
    const auto selection = waveform.getSelection();
    applyEdit([this, selection]
    {
        if (! clip.deleteRange(selection))
            return false;
        waveform.setSelection({});
        waveform.setCursor(selection.getStart());
        return true;
    });
}

void MicRecorderComponent::trimToSelection()
{
    const auto selection = waveform.getSelection();
    applyEdit([this, selection]
    {
        if (! clip.keepOnly(selection))
            return false;
        waveform.setSelection({});
        waveform.setCursor(0);
        waveform.zoomToFit();
        return true;
    });
}

void MicRecorderComponent::undo()
{
    applyEdit([this] { return clip.undo(); });
}

void MicRecorderComponent::redo()
{
    applyEdit([this] { return clip.redo(); });
}

void MicRecorderComponent::newClip()
{
    if (recorder.isRecording())
        return;

    confirmDiscard([this]
    {
        playback.stop();
        clip.clear();
        noiseProfile = {};
        dirty = false;
        waveform.setSelection({});
        waveform.setCursor(0);
        waveform.clipChanged();
        updateButtons();
        updateTimeLabels();
    });
}

// ==============================================================================
// Clean-up tools
// ==============================================================================

void MicRecorderComponent::showMakeMonoMenu()
{
    if (clip.getNumChannels() < 2)
    {
        showStatus("This take is already mono.");
        return;
    }

    const int autoSource = ClipEffects::chooseMonoSource(clip.getBuffer(), clip.getNumSamples());
    const juce::String autoText = autoSource == 0 ? "keeps left" : autoSource == 1 ? "keeps right" : "mixes both";

    juce::PopupMenu menu;
    menu.addItem(1, "Auto (" + autoText + ")");
    menu.addItem(2, "Keep left channel");
    menu.addItem(3, "Keep right channel");
    menu.addItem(4, "Mix both channels");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&makeMonoButton),
        [this, autoSource](int result)
        {
            if (result == 0)
                return;
            const int source = result == 1 ? autoSource : result == 2 ? 0 : result == 3 ? 1 : -1;
            applyEdit([this, source] { return clip.makeMono(source); });
        });
}

void MicRecorderComponent::removeHum()
{
    const double mainsHz = ClipEffects::detectMainsHz(clip.getBuffer(), clip.getNumSamples(), clip.getSampleRate());
    const double rate = clip.getSampleRate();

    applyEdit([this, mainsHz, rate]
    {
        return clip.applyEffect(waveform.getSelection(), [mainsHz, rate](juce::AudioBuffer<float>& audio, int start, int length)
        {
            ClipEffects::removeHum(audio, start, length, rate, mainsHz);
            return true;
        });
    });
    showStatus("Removed " + juce::String((int) mainsHz) + " Hz hum, its harmonics and rumble below 70 Hz.");
}

void MicRecorderComponent::learnNoiseProfile()
{
    auto selection = waveform.getSelection();
    bool picked = false;

    if (selection.isEmpty())
    {
        // Nothing selected: use the quietest half second, and show it.
        const int length = (int) (clip.getSampleRate() * 0.5);
        const auto start = ClipEffects::findQuietestStretch(clip.getBuffer(), clip.getNumSamples(), length);
        if (start < 0)
        {
            showStatus("The take is too short to find a quiet part in -- record a little longer.");
            return;
        }
        selection = { start, start + length };
        waveform.setSelection(selection);
        picked = true;
    }

    if (selection.getLength() < ClipEffects::fftSize)
    {
        showStatus("Select a longer stretch (at least 0.1 s) that contains only background noise.");
        return;
    }

    noiseProfile = ClipEffects::learnNoiseProfile(clip.getBuffer(), (int) selection.getStart(),
                                                  (int) selection.getLength());

    if (picked)
    {
        // The highlighted stretch is only the sample; clear it so Reduce Noise
        // cleans the whole take.
        showStatus("Learned the noise from the quietest part (" + formatTime(selection.getStart())
                   + "). Now click Reduce Noise to clean the whole take.");
        juce::Timer::callAfterDelay(1500, [safe = juce::Component::SafePointer<MicRecorderComponent>(this), selection]
        {
            if (safe != nullptr && safe->waveform.getSelection() == selection)
                safe->waveform.setSelection({});
        });
    }
    else
    {
        showStatus("Noise profile learned from " + formatTime(selection.getLength())
                   + ". Now select what to clean (or nothing for the whole take) and click Reduce Noise.");
    }
    updateButtons();
}

void MicRecorderComponent::showReduceNoiseMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader(noiseProfile.isValid() ? "Using your noise profile"
                                                 : "Learning the noise from the quietest parts");
    menu.addItem(1, "Light (-6 dB)");
    menu.addItem(2, "Medium (-12 dB)");
    menu.addItem(3, "Strong (-20 dB)");
    if (noiseProfile.isValid())
    {
        menu.addSeparator();
        menu.addItem(9, "Forget noise profile");
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&reduceNoiseButton),
        [this](int result)
        {
            if (result == 9)
            {
                noiseProfile = {};
                showStatus("Noise profile cleared; Reduce Noise will find the quiet parts itself.");
            }
            else if (result >= 1 && result <= 3)
            {
                reduceNoise(result == 1 ? 6.0f : result == 2 ? 12.0f : 20.0f);
            }
        });
}

void MicRecorderComponent::reduceNoise(float reductionDb)
{
    auto profile = noiseProfile;
    if (! profile.isValid())
        profile = ClipEffects::estimateNoiseProfile(clip.getBuffer(), 0, (int) clip.getNumSamples());

    if (! profile.isValid())
    {
        showStatus("The take is too short (or silent) to learn the noise from.");
        return;
    }

    juce::MouseCursor::showWaitCursor();
    applyEdit([this, &profile, reductionDb]
    {
        return clip.applyEffect(waveform.getSelection(), [&profile, reductionDb](juce::AudioBuffer<float>& audio, int start, int length)
        {
            ClipEffects::reduceNoise(audio, start, length, profile, reductionDb);
            return true;
        });
    });
    juce::MouseCursor::hideWaitCursor();
    showStatus("Background noise turned down by " + juce::String((int) reductionDb)
               + " dB. Ctrl+Z to undo; apply again for more.");
}

void MicRecorderComponent::showStatus(const juce::String& text)
{
    hintLabel.setText(text, juce::dontSendNotification);
    hintLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffd54f));
    statusUntilMs = juce::Time::getMillisecondCounter() + 8000;
}

static juce::File getChannelModeFile()
{
    return getAppDataDirectory()
               .getChildFile("mic_recorder.xml");
}

void MicRecorderComponent::loadChannelMode()
{
    if (auto xml = juce::XmlDocument::parse(getChannelModeFile()))
        recorder.setChannelMode((MicRecorder::ChannelMode) juce::jlimit(0, 3, xml->getIntAttribute("channelMode", 0)));
}

void MicRecorderComponent::saveChannelMode() const
{
    juce::XmlElement xml("MicRecorder");
    xml.setAttribute("channelMode", (int) recorder.getChannelMode());
    getChannelModeFile().getParentDirectory().createDirectory();
    xml.writeTo(getChannelModeFile());
}

// ==============================================================================
// Input device, saving, closing
// ==============================================================================

void MicRecorderComponent::refreshInputLabel()
{
    const auto description = recorder.describeCurrentInput();

    if (inputError.isNotEmpty() || ! recorder.hasUsableInput())
    {
        inputLabel.setText("No microphone open" + (inputError.isNotEmpty() ? ": " + inputError : juce::String())
                               + "  -  click \"Input device...\"",
                           juce::dontSendNotification);
        inputLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef5350));
    }
    else
    {
        inputLabel.setText("Input: " + description + "   >  recording " + recorder.describeRecordSource(),
                           juce::dontSendNotification);
        inputLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    }
}

void MicRecorderComponent::showInputDeviceDialog()
{
    if (recorder.isRecording())
        return;

    // Inputs only (1-2 channels), no outputs: this device is just the mic.
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        recorder.getDeviceManager(), 1, 2, 0, 0, false, false, false, false);
    selector->setSize(520, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Microphone input";
    options.dialogBackgroundColour = panelColour;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();

    // The selector applies changes live; the label is refreshed by the timer.
    inputError.clear();
}

void MicRecorderComponent::saveClip(std::function<void()> afterSave)
{
    if (clip.isEmpty() || recorder.isRecording())
        return;

    playback.stop();
    const auto defaultName = "Mic recording " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + ".wav";
    fileChooser = std::make_unique<juce::FileChooser>("Save recording as...",
                                                      lastSaveDirectory.getChildFile(defaultName),
                                                      "*.wav;*.flac");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, afterSave](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File{})
                return;

            if (! file.hasFileExtension(".wav;.flac"))
                file = file.withFileExtension(".wav");

            juce::String error;
            if (! clip.writeToFile(file, error))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save", error);
                return;
            }

            dirty = false;
            lastSaveDirectory = file.getParentDirectory();
            if (addToPlaylistToggle.getToggleState() && onAddToPlaylist)
                onAddToPlaylist(file);
            if (afterSave)
                afterSave();
        });
}

void MicRecorderComponent::confirmDiscard(std::function<void()> onDiscardOrSaved)
{
    if (! dirty || clip.isEmpty())
    {
        onDiscardOrSaved();
        return;
    }

    juce::AlertWindow::showYesNoCancelBox(
        juce::MessageBoxIconType::QuestionIcon, "Unsaved recording",
        "The current recording hasn't been saved.",
        "Save...", "Discard", "Cancel", this,
        juce::ModalCallbackFunction::create([this, onDiscardOrSaved](int result)
        {
            if (result == 1)
                saveClip(onDiscardOrSaved);
            else if (result == 2)
                onDiscardOrSaved();
        }));
}

void MicRecorderComponent::requestClose()
{
    stopAll();
    confirmDiscard([this]
    {
        if (onCloseConfirmed)
            onCloseConfirmed();
    });
}

// ==============================================================================
// State display
// ==============================================================================

juce::String MicRecorderComponent::formatTime(juce::int64 samples) const
{
    const double rate = clip.getSampleRate() > 0.0 ? clip.getSampleRate() : 48000.0;
    const double seconds = (double) samples / rate;
    const int minutes = (int) (seconds / 60.0);
    return juce::String(minutes) + ":" + juce::String(seconds - minutes * 60.0, 2).paddedLeft('0', 5);
}

void MicRecorderComponent::updateTimeLabels()
{
    const auto position = playback.isPlaying() ? (juce::int64) playback.getPositionSamples() : waveform.getCursor();
    juce::String time = recorder.isRecording() ? "REC  " + formatTime(clip.getNumSamples())
                                               : formatTime(position) + " / " + formatTime(clip.getNumSamples());
    timeLabel.setText(time, juce::dontSendNotification);
    timeLabel.setColour(juce::Label::textColourId, recorder.isRecording() ? juce::Colours::red : juce::Colours::lightgrey);

    const auto selection = waveform.getSelection();
    selectionLabel.setText(selection.isEmpty() ? juce::String()
                                               : "Selection " + formatTime(selection.getStart()) + " - "
                                                     + formatTime(selection.getEnd()) + "  ("
                                                     + formatTime(selection.getLength()) + ")",
                           juce::dontSendNotification);

    refreshInputLabel();
}

void MicRecorderComponent::updateButtons()
{
    const bool recording = recorder.isRecording();
    const bool hasAudio = ! clip.isEmpty();
    const bool hasSelection = ! waveform.getSelection().isEmpty();
    const bool canEdit = hasAudio && ! recording;

    recordButton.setButtonText(recording ? "Stop Rec" : "Record");
    recordButton.setColour(juce::TextButton::buttonColourId, recording ? juce::Colours::red : recordColour);
    recordButton.setEnabled(recording || recorder.hasUsableInput());

    playButton.setButtonText(playback.isPlaying() ? "Pause" : "Play");
    playButton.setEnabled(canEdit);
    stopButton.setEnabled(recording || playback.isPlaying());

    undoButton.setEnabled(! recording && clip.canUndo());
    redoButton.setEnabled(! recording && clip.canRedo());
    for (auto* b : { &trimButton, &deleteButton, &silenceButton, &fadeInButton, &fadeOutButton, &zoomSelButton })
        b->setEnabled(canEdit && hasSelection);
    for (auto* b : { &normalizeButton, &zoomInButton, &zoomOutButton, &zoomFitButton, &saveButton,
                     &removeHumButton, &reduceNoiseButton })
        b->setEnabled(canEdit);
    makeMonoButton.setEnabled(canEdit && clip.getNumChannels() > 1);
    noiseProfileButton.setEnabled(canEdit);
    channelModeBox.setEnabled(! recording);
    newButton.setEnabled(canEdit);
    inputDeviceButton.setEnabled(! recording);
}

bool MicRecorderComponent::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool cmd = mods.isCommandDown() || mods.isCtrlDown();
    const int code = key.getKeyCode();
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

    if (code == juce::KeyPress::spaceKey)                  { togglePlayback(); return true; }
    if (! cmd && ch == 'r')                                 { toggleRecording(); return true; }
    if (code == juce::KeyPress::escapeKey)                  { stopAll(); waveform.setSelection({}); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
                                                            { deleteSelection(); return true; }
    if (cmd && (code == 'Z' || ch == 'z'))                 { mods.isShiftDown() ? redo() : undo(); return true; }
    if (cmd && (code == 'Y' || ch == 'y'))                 { redo(); return true; }
    if (cmd && (code == 'A' || ch == 'a'))                 { waveform.selectAll(); return true; }
    if (cmd && (code == 'S' || ch == 's'))                 { saveClip(); return true; }
    if (cmd && (code == 'T' || ch == 't'))                 { trimToSelection(); return true; }
    if (code == juce::KeyPress::homeKey)                    { waveform.setSelection({}); waveform.setCursor(0); return true; }
    if (code == juce::KeyPress::endKey)                     { waveform.setSelection({}); waveform.setCursor(clip.getNumSamples()); return true; }
    if (! cmd && (ch == '+' || ch == '='))                  { waveform.zoomIn(); return true; }
    if (! cmd && ch == '-')                                 { waveform.zoomOut(); return true; }
    if (! cmd && ch == 'f')                                 { waveform.zoomToFit(); return true; }

    return false;
}

// ==============================================================================
// Window
// ==============================================================================

MicRecorderWindow::MicRecorderWindow(AudioEngine& engine,
                                     std::function<void(const juce::File&)> addToPlaylist,
                                     std::function<void()> onClosed)
    : juce::DocumentWindow("Record from microphone", panelColour, juce::DocumentWindow::allButtons)
{
    auto content = std::make_unique<MicRecorderComponent>(engine);
    content->onAddToPlaylist = std::move(addToPlaylist);
    content->onCloseConfirmed = std::move(onClosed);
    recorderContent = content.get();

    setUsingNativeTitleBar(true);
    setContentOwned(content.release(), true);
    setResizable(true, false);
    setResizeLimits(1160, 420, 4000, 2000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    recorderContent->grabKeyboardFocus();
}

void MicRecorderWindow::closeButtonPressed()
{
    recorderContent->requestClose();
}
