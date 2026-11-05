#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Audio/AudioEngine.h"
#include "../Recorder/MicClip.h"
#include "../Recorder/MicRecorder.h"
#include "../Recorder/ClipPlaybackSource.h"
#include "../Recorder/ClipEffects.h"
#include "WaveformEditorComponent.h"

// The "Record from microphone" window: pick an input, watch its level, record
// takes into one clip, then play, select and edit it and save it as WAV/FLAC
// (optionally adding the file to the playlist).
//
// Takes are previewed through AudioEngine's auxiliary source, i.e. on the
// normal output device. The mic itself is opened by MicRecorder only while
// this window exists.
class MicRecorderComponent : public juce::Component,
                             private juce::Timer
{
public:
    explicit MicRecorderComponent(AudioEngine& engine);
    ~MicRecorderComponent() override;

    // Asks before throwing away an unsaved take, then calls onCloseConfirmed.
    void requestClose();

    std::function<void()> onCloseConfirmed;
    // Called with each file saved while "Add to playlist" is ticked.
    std::function<void(const juce::File&)> onAddToPlaylist;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class LevelMeter;

    void timerCallback() override;

    void toggleRecording();
    void startRecording();
    void stopRecording();
    void togglePlayback();
    void startPlayback();
    void stopAll();

    // Runs one edit; on success marks the take dirty and refreshes the view.
    void applyEdit(const std::function<bool()>& edit);
    void deleteSelection();
    void trimToSelection();
    void undo();
    void redo();
    void newClip();

    // Clean-up tools (see ClipEffects).
    void showMakeMonoMenu();
    void removeHum();
    void learnNoiseProfile();
    void showReduceNoiseMenu();
    void reduceNoise(float reductionDb);

    // Shows text in place of the shortcut hint for a few seconds.
    void showStatus(const juce::String& text);

    void loadChannelMode();
    void saveChannelMode() const;

    void showInputDeviceDialog();
    // Saves via a file chooser; afterSave runs only if a file was written.
    void saveClip(std::function<void()> afterSave = {});
    void confirmDiscard(std::function<void()> onDiscardOrSaved);

    void refreshInputLabel();
    void updateButtons();
    void updateTimeLabels();
    juce::String formatTime(juce::int64 samples) const;

    AudioEngine& audioEngine;
    MicClip clip;
    MicRecorder recorder;
    ClipPlaybackSource playback { clip };
    juce::String inputError;
    bool dirty = false;

    WaveformEditorComponent waveform { clip };
    std::unique_ptr<LevelMeter> levelMeter;

    juce::Label inputLabel;
    juce::TextButton inputDeviceButton { "Input device..." };
    juce::ComboBox channelModeBox;

    juce::TextButton recordButton { "Record" };
    juce::TextButton playButton   { "Play" };
    juce::TextButton stopButton   { "Stop" };
    juce::Label timeLabel;
    juce::Label selectionLabel;

    juce::TextButton undoButton      { "Undo" };
    juce::TextButton redoButton      { "Redo" };
    juce::TextButton trimButton      { "Trim" };
    juce::TextButton deleteButton    { "Delete" };
    juce::TextButton silenceButton   { "Silence" };
    juce::TextButton fadeInButton    { "Fade In" };
    juce::TextButton fadeOutButton   { "Fade Out" };
    juce::TextButton normalizeButton { "Normalize" };

    juce::TextButton zoomInButton    { "Zoom +" };
    juce::TextButton zoomOutButton   { "Zoom -" };
    juce::TextButton zoomFitButton   { "Fit" };
    juce::TextButton zoomSelButton   { "Zoom Sel" };

    juce::TextButton makeMonoButton     { "Make Mono" };
    juce::TextButton removeHumButton    { "Remove Hum" };
    juce::TextButton noiseProfileButton { "Noise Profile" };
    juce::TextButton reduceNoiseButton  { "Reduce Noise" };
    ClipEffects::NoiseProfile noiseProfile;

    juce::TextButton newButton       { "New" };
    juce::TextButton saveButton      { "Save..." };
    juce::ToggleButton addToPlaylistToggle { "Add to playlist" };

    juce::Label hintLabel;
    juce::String hintText;
    juce::uint32 statusUntilMs = 0;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File lastSaveDirectory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicRecorderComponent)
};

// Top-level window hosting a MicRecorderComponent.
class MicRecorderWindow : public juce::DocumentWindow
{
public:
    MicRecorderWindow(AudioEngine& engine,
                      std::function<void(const juce::File&)> addToPlaylist,
                      std::function<void()> onClosed);

    void closeButtonPressed() override;

private:
    MicRecorderComponent* recorderContent = nullptr; // owned by DocumentWindow

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicRecorderWindow)
};
