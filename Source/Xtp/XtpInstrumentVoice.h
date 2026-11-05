#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace xtp
{

// One instrument's voice pool: renders audio for note-on/off events dispatched
// by XtpSequencer. Implementations port exTracker's IInstrumentPlugin
// subclasses (extracker/src/plugin_host.cpp) closely enough to sound the same;
// see per-class comments for the specific formulas carried over.
class InstrumentVoice
{
public:
    virtual ~InstrumentVoice() = default;

    // velocity is 1-127. If retrigger is false and a voice for this midiNote is
    // already sounding, its pitch/level are updated in place rather than
    // restarted -- this is how per-tick modulation (vibrato/slides/tremolo) is
    // applied: the sequencer calls noteOn() every tick with retrigger=false.
    virtual void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) = 0;
    virtual void noteOff(int midiNote) = 0;
    virtual void allNotesOff() = 0;

    // Adds this instrument's rendered output into monoBuffer (mixed in, not overwritten).
    virtual void renderAdd(std::vector<float>& monoBuffer, double sampleRate) = 0;

    virtual bool hasActiveVoices() const = 0;
};

// Additive sine/square oscillator with a linear attack/release envelope. Port of
// exTracker's BuiltinInstrumentPluginBase (plugin_host.cpp:375-523): identical
// note->frequency formula, attack/release step sizes, and per-frame
// 1/voiceCount mix normalization.
class BuiltinOscillatorVoice : public InstrumentVoice
{
public:
    enum class Waveform { Sine, Square };
    explicit BuiltinOscillatorVoice(Waveform w) : waveform(w) {}

    void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) override;
    void noteOff(int midiNote) override;
    void allNotesOff() override;
    void renderAdd(std::vector<float>& monoBuffer, double sampleRate) override;
    bool hasActiveVoices() const override { return !voices.empty(); }

private:
    struct Voice
    {
        int midiNote = 0;
        double frequencyHz = 440.0;
        double phase = 0.0;
        double level = 0.0;
        double targetLevel = 0.0;
        bool releasing = false;
    };

    Waveform waveform;
    std::vector<Voice> voices;
    static constexpr double gain = 1.0;
    static constexpr double attackMs = 5.0;
    static constexpr double releaseMs = 60.0;
};

// WAV sample playback. Port of exTracker's BuiltinSamplePlugin
// (plugin_host.cpp, class BuiltinSamplePlugin): pitch by resampling ratio,
// linear interpolation, fixed 30ms release envelope, and the forward /
// ping-pong / sustain loop modes. A bank slot played directly is one-shot at
// root C-4; a sample instrument's INSTRUMENT_PARAM lines set gain, root and
// loop through setParameter().
class SampleVoice : public InstrumentVoice
{
public:
    // Decodes the WAV via JUCE's own reader (soundplayer2 already links
    // juce_audio_formats) rather than hand-rolling a parser as exTracker does --
    // this is decode-only with no playback-timing implications.
    bool loadSample(const juce::File& file);
    bool isLoaded() const { return sampleRate > 0.0 && !mono.empty(); }

    // Same names and clamping as BuiltinSamplePlugin::setParameter: "gain",
    // "sample_root", "loop_mode" (0 off, 1 forward, 2 ping-pong, 3 sustain),
    // "loop_start", "loop_end" (frames; <= 0 = end of sample). "pan" is
    // accepted and ignored (soundplayer2 mixes xtp voices to mono).
    void setParameter(const std::string& name, double value);

    void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) override;
    void noteOff(int midiNote) override;
    void allNotesOff() override;
    void renderAdd(std::vector<float>& monoBuffer, double sampleRate) override;
    bool hasActiveVoices() const override { return !voices.empty(); }

private:
    struct Voice
    {
        int midiNote = 0;
        double pos = 0.0;
        double pitchRatio = 1.0;
        double level = 0.0;
        double envelope = 1.0;
        bool releasing = false;
        bool active = true;
        double direction = 1.0;
    };

    std::vector<float> mono;
    double sampleRate = 0.0;
    int rootMidiNote = 60;
    double gain = 1.0;
    int loopMode = 0;
    std::size_t loopStart = 0;
    std::size_t loopEnd = std::numeric_limits<std::size_t>::max();

    std::vector<Voice> voices;
};

// Silent placeholder for instrument slots that reference a backend not yet
// implemented (sf2:/sfz:/lv2:/vst3.) -- logged once at load time, then plays
// nothing, rather than crashing or aborting the whole song.
class SilentVoice : public InstrumentVoice
{
public:
    void noteOn(int, std::uint8_t, bool) override {}
    void noteOff(int) override {}
    void allNotesOff() override {}
    void renderAdd(std::vector<float>&, double) override {}
    bool hasActiveVoices() const override { return false; }
};

} // namespace xtp
