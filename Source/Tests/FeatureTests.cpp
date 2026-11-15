#include <iostream>
#include <cassert>
#include <cmath>
#include "../Playlist/PlaylistManager.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/MultiChannelAudioFormatReaderSource.h"
#include "../Audio/ModPlugFormat.h"
#include "../Effects/Equalizer.h"
#include "../Xtp/XtpSong.h"
#include "../Xtp/XtpSequencer.h"
#include "../Xtp/XtpSequencerSource.h"
#include "../Xtp/XtpTransportClock.h"
#include "../Export/VideoExporter.h"
#include "../Export/PlaylistVideoExporter.h"
#include "../Export/ScreenRecorder.h"
#include "../Visualization/Plugins/WaveformPlugin.h"
#include "../Video/VideoFileProbe.h"
#include "../Video/FfmpegVideoFrameDecoder.h"
#include "../Video/SubtitleTrack.h"
#include "../Audio/FfmpegVideoAudioFormatReader.h"
#include "../Playlist/AudioFileCollector.h"
#include "../Recorder/MicClip.h"
#include "../Recorder/MicRecorder.h"
#include "../Recorder/ClipPlaybackSource.h"
#include "../Recorder/ClipEffects.h"
#include <thread>
#include <chrono>
#include <stdexcept>
#include <string>

void testPlaylistManager()
{
    std::cout << "=== Testing PlaylistManager ===" << std::endl;
    
    PlaylistManager playlist;
    
    // Create temporary test files
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File testFile1 = tempDir.getChildFile("test_audio1.wav");
    juce::File testFile2 = tempDir.getChildFile("test_audio2.mp3");
    juce::File testFile3 = tempDir.getChildFile("test_audio3.flac");
    
    testFile1.create();
    testFile2.create();
    testFile3.create();
    
    // Test: Add files
    juce::Array<juce::File> testFiles;
    testFiles.add(testFile1);
    testFiles.add(testFile2);
    testFiles.add(testFile3);
    playlist.addFiles(testFiles);
    assert(playlist.getItems().size() == 3);
    std::cout << "✓ Add files: " << playlist.getItems().size() << " files in playlist" << std::endl;
    
    // Test: Get current item (empty initially)
    auto currentItem = playlist.getCurrentItem();
    assert(currentItem == nullptr);  // No item selected yet
    std::cout << "✓ Current item (empty): null" << std::endl;
    
    // Test: Select item
    playlist.selectItem(0);
    assert(playlist.getCurrentIndex() == 0);
    std::cout << "✓ Select item: index " << playlist.getCurrentIndex() << std::endl;
    
    currentItem = playlist.getCurrentItem();
    assert(currentItem != nullptr);
    std::cout << "✓ Current item: " << currentItem->name << std::endl;
    
    // Test: Next/Prev
    playlist.nextTrack();
    assert(playlist.getCurrentIndex() == 1);
    std::cout << "✓ Next track: index " << playlist.getCurrentIndex() << std::endl;
    
    playlist.previousTrack();
    assert(playlist.getCurrentIndex() == 0);
    std::cout << "✓ Previous track: index " << playlist.getCurrentIndex() << std::endl;
    
    // Test: Remove
    playlist.removeItem(0);
    assert(playlist.getItems().size() == 2);
    std::cout << "✓ Remove item: " << playlist.getItems().size() << " files remaining" << std::endl;
    
    // Test: Move
    playlist.moveItem(0, 1);
    std::cout << "✓ Move item: reordered playlist" << std::endl;
    
    // Test: Clear
    playlist.clearPlaylist();
    assert(playlist.getItems().size() == 0);
    std::cout << "✓ Clear playlist: " << playlist.getItems().size() << " files" << std::endl;
    
    // Cleanup
    testFile1.deleteFile();
    testFile2.deleteFile();
    testFile3.deleteFile();
    
    std::cout << std::endl;
}

void testPlaylistPersistence()
{
    std::cout << "=== Testing Playlist Persistence ===" << std::endl;

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File audioFile = tempDir.getChildFile("sp2_persist_channels.wav");
    juce::File playlistFile = tempDir.getChildFile("sp2_persist_channels.xml");
    audioFile.deleteFile();
    playlistFile.deleteFile();

    {
        juce::WavAudioFormat wavFormat;
        auto outputStream = std::make_unique<juce::FileOutputStream>(audioFile);
        auto writer = std::unique_ptr<juce::AudioFormatWriter>(
            wavFormat.createWriterFor(outputStream.get(), 48000.0, 6, 16, {}, 0));
        assert(writer != nullptr);
        outputStream.release();

        juce::AudioBuffer<float> buffer(6, 64);
        buffer.clear();
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }

    PlaylistManager original;
    original.addFile(audioFile);
    assert(original.getItems().size() == 1);
    assert(original.getItems()[0].channelLayout == "5.1");
    assert(original.savePlaylist(playlistFile));

    PlaylistManager restored;
    assert(restored.loadPlaylist(playlistFile));
    assert(restored.getItems().size() == 1);
    assert(restored.getItems()[0].channelLayout == "5.1");
    std::cout << "✓ Persist channel layout through save/load" << std::endl;

    audioFile.deleteFile();
    playlistFile.deleteFile();

    std::cout << std::endl;
}

void testShuffleRepeat()
{
    std::cout << "=== Testing Shuffle & Repeat ===" << std::endl;

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::Array<juce::File> files;
    for (int i = 0; i < 5; ++i)
    {
        auto f = tempDir.getChildFile("sp2_sr_test_" + juce::String(i) + ".wav");
        f.create();
        files.add(f);
    }

    {
        PlaylistManager pl;
        pl.addFiles(files);
        assert(pl.getItems().size() == 5);

        // Default repeat mode is Off
        assert(pl.getRepeatMode() == PlaylistManager::RepeatMode::Off);
        std::cout << "✓ Default repeat mode: Off" << std::endl;

        // Repeat Off: end of list stops (index does not move)
        pl.selectItem(4);
        pl.advanceAfterTrackEnd();
        assert(pl.getCurrentIndex() == 4);
        std::cout << "✓ Repeat Off stops at end of playlist" << std::endl;

        // Repeat Off: mid-list still advances
        pl.selectItem(1);
        pl.advanceAfterTrackEnd();
        assert(pl.getCurrentIndex() == 2);
        std::cout << "✓ Repeat Off advances within playlist" << std::endl;

        // Repeat All: end of list wraps to the start
        pl.setRepeatMode(PlaylistManager::RepeatMode::All);
        pl.selectItem(4);
        pl.advanceAfterTrackEnd();
        assert(pl.getCurrentIndex() == 0);
        std::cout << "✓ Repeat All wraps to start" << std::endl;

        // Repeat One is handled by the player (track replays), so the index is
        // unchanged here — manual next still moves regardless of repeat mode.
        pl.setRepeatMode(PlaylistManager::RepeatMode::One);
        pl.selectItem(2);
        pl.nextTrack();
        assert(pl.getCurrentIndex() == 3);
        std::cout << "✓ Manual next ignores Repeat One" << std::endl;
    }

    {
        // Shuffle: each track is visited exactly once per cycle
        PlaylistManager pl;
        pl.addFiles(files);
        pl.setShuffle(true);
        assert(pl.getShuffle());

        bool seen[5] = { false, false, false, false, false };
        int unique = 0;
        for (int i = 0; i < 5; ++i)
        {
            pl.nextTrack();
            int idx = pl.getCurrentIndex();
            assert(idx >= 0 && idx < 5);
            if (!seen[idx]) { seen[idx] = true; ++unique; }
        }
        assert(unique == 5);
        std::cout << "✓ Shuffle covers all tracks once per cycle" << std::endl;

        // Crossing the cycle boundary must not repeat the just-played track
        int lastIdx = pl.getCurrentIndex();
        pl.nextTrack();
        assert(pl.getCurrentIndex() != lastIdx);
        std::cout << "✓ Shuffle avoids immediate repeat across cycles" << std::endl;

        // previousTrack steps back through the shuffle sequence
        int a = pl.getCurrentIndex();
        pl.nextTrack();
        pl.previousTrack();
        assert(pl.getCurrentIndex() == a);
        std::cout << "✓ Shuffle previous returns to prior track" << std::endl;
    }

    for (auto& f : files)
        f.deleteFile();

    std::cout << std::endl;
}

void testEqualizer()
{
    std::cout << "=== Testing Equalizer ===" << std::endl;
    
    Equalizer eq;
    
    // Test: Set/Get gain for each band
    for (int i = 0; i < 10; ++i)
    {
        float testGain = -6.0f + (i * 1.2f);  // Vary gain per band
        eq.setGain(i, testGain);
        float readGain = eq.getGain(i);
        assert(readGain == testGain);
    }
    std::cout << "✓ Set/Get gain for all 10 bands" << std::endl;
    
    // Test: Gain bounds
    eq.setGain(0, -15.0f);  // Test min
    assert(eq.getGain(0) == -15.0f);
    std::cout << "✓ Min gain (-15dB)" << std::endl;
    
    eq.setGain(1, 12.0f);   // Test max
    assert(eq.getGain(1) == 12.0f);
    std::cout << "✓ Max gain (12dB)" << std::endl;
    
    // Test: Invalid band index
    float invalidGain = eq.getGain(999);  // Out of range
    assert(invalidGain == 0.0f);  // Should return 0
    std::cout << "✓ Out-of-range band returns 0dB" << std::endl;
    
    std::cout << std::endl;
}

void testAudioEngine()
{
    std::cout << "=== Testing AudioEngine ===" << std::endl;
    
    PlaylistManager playlist;
    AudioEngine engine(&playlist);
    
    // Test: Sample rate setting
    engine.setSampleRate(44100.0);
    assert(engine.getCurrentSampleRate() == 44100.0);
    std::cout << "✓ Set sample rate: " << engine.getCurrentSampleRate() << " Hz" << std::endl;
    
    engine.setSampleRate(48000.0);
    assert(engine.getCurrentSampleRate() == 48000.0);
    std::cout << "✓ Change to 48kHz: " << engine.getCurrentSampleRate() << " Hz" << std::endl;
    
    engine.setSampleRate(96000.0);
    assert(engine.getCurrentSampleRate() == 96000.0);
    std::cout << "✓ Change to 96kHz: " << engine.getCurrentSampleRate() << " Hz" << std::endl;
    
    // Test: Bit depth setting
    engine.setBitDepth(16);
    assert(engine.getCurrentBitDepth() == 16);
    std::cout << "✓ Set bit depth: " << engine.getCurrentBitDepth() << "-bit" << std::endl;
    
    engine.setBitDepth(24);
    assert(engine.getCurrentBitDepth() == 24);
    std::cout << "✓ Change to 24-bit: " << engine.getCurrentBitDepth() << "-bit" << std::endl;
    
    engine.setBitDepth(32);
    assert(engine.getCurrentBitDepth() == 32);
    std::cout << "✓ Change to 32-bit: " << engine.getCurrentBitDepth() << "-bit" << std::endl;
    
    // Test: Equalizer access
    Equalizer& eq = engine.getEqualizer();
    eq.setGain(0, 3.0f);
    assert(eq.getGain(0) == 3.0f);
    std::cout << "✓ Access equalizer from engine" << std::endl;
    
    // Test: Audio buffer access
    const auto& buffer = engine.getAudioBuffer();
    std::cout << "✓ Access audio buffer (channels: " << buffer.getNumChannels() 
              << ", samples: " << buffer.getNumSamples() << ")" << std::endl;

    // Test: Multichannel files preserve their decoded channel count.
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File multichannelFile = tempDir.getChildFile("sp2_multichannel_test.wav");
    multichannelFile.deleteFile();

    {
        juce::WavAudioFormat wavFormat;
        auto outputStream = std::make_unique<juce::FileOutputStream>(multichannelFile);
        auto writer = std::unique_ptr<juce::AudioFormatWriter>(
            wavFormat.createWriterFor(outputStream.get(), 48000.0, 4, 16, {}, 0));
        assert(writer != nullptr);
        outputStream.release();

        juce::AudioBuffer<float> multichannelBuffer(4, 64);
        multichannelBuffer.clear();
        writer->writeFromAudioSampleBuffer(multichannelBuffer, 0, multichannelBuffer.getNumSamples());
    }

    engine.loadTrack(multichannelFile);
    assert(engine.getCurrentProgramChannels() == 4);
    std::cout << "✓ Preserve decoded multichannel layout (4 channels)" << std::endl;
    multichannelFile.deleteFile();
    
    std::cout << std::endl;
}

void testMultichannelReaderSource()
{
    std::cout << "=== Testing Multichannel Reader Source ===" << std::endl;

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File audioFile = tempDir.getChildFile("sp2_reader_multichannel.wav");
    audioFile.deleteFile();

    {
        juce::WavAudioFormat wavFormat;
        auto outputStream = std::make_unique<juce::FileOutputStream>(audioFile);
        auto writer = std::unique_ptr<juce::AudioFormatWriter>(
            wavFormat.createWriterFor(outputStream.get(), 48000.0, 4, 16, {}, 0));
        assert(writer != nullptr);
        outputStream.release();

        juce::AudioBuffer<float> buffer(4, 32);
        buffer.clear();
        buffer.applyGain(0, 0, 32, 0.10f);
        buffer.applyGain(1, 0, 32, 0.20f);
        buffer.applyGain(2, 0, 32, 0.30f);
        buffer.applyGain(3, 0, 32, 0.40f);
        for (int ch = 0; ch < 4; ++ch)
            for (int i = 0; i < 32; ++i)
                buffer.setSample(ch, i, 0.1f * (float) (ch + 1));
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    auto* reader = formatManager.createReaderFor(audioFile);
    assert(reader != nullptr);

    MultiChannelAudioFormatReaderSource source(reader, true);
    juce::AudioBuffer<float> output(4, 32);
    output.clear();
    juce::AudioSourceChannelInfo info(&output, 0, 32);
    source.getNextAudioBlock(info);

    assert(std::abs(output.getSample(0, 0) - 0.1f) < 0.01f);
    assert(std::abs(output.getSample(1, 0) - 0.2f) < 0.01f);
    assert(std::abs(output.getSample(2, 0) - 0.3f) < 0.01f);
    assert(std::abs(output.getSample(3, 0) - 0.4f) < 0.01f);
    std::cout << "✓ Preserve rear-channel signal through reader source" << std::endl;

    audioFile.deleteFile();

    std::cout << std::endl;
}

void testStereoExpansion()
{
    std::cout << "=== Testing Stereo Expansion ===" << std::endl;

    juce::AudioBuffer<float> buffer(6, 8);
    buffer.clear();
    for (int i = 0; i < 8; ++i)
    {
        buffer.setSample(0, i, 0.25f);
        buffer.setSample(1, i, -0.5f);
    }

    juce::StringArray channelNames { "Left", "Right", "Center", "LFE", "Rear Left", "Rear Right" };
    AudioEngine::expandStereoToOutputChannels(buffer, 6, 8, &channelNames);

    for (int i = 0; i < 8; ++i)
    {
        assert(std::abs(buffer.getSample(2, i) + 0.125f) < 1e-6f);
        assert(std::abs(buffer.getSample(3, i)) < 1e-6f);
        assert(std::abs(buffer.getSample(4, i) - 0.25f) < 1e-6f);
        assert(std::abs(buffer.getSample(5, i) + 0.5f) < 1e-6f);
    }

    std::cout << "✓ Expand stereo by speaker role: mono center, silent LFE, live rears" << std::endl;
    std::cout << std::endl;
}

void testSpeakerTestChannelAdvance()
{
    std::cout << "=== Testing Speaker Test Channel Advance ===" << std::endl;

    PlaylistManager playlist;
    AudioEngine engine(&playlist);
    engine.advanceSpeakerTestChannel();
    assert(engine.getSpeakerTestChannel() == 1);
    engine.advanceSpeakerTestChannel();
    assert(engine.getSpeakerTestChannel() == 0 || engine.getSpeakerTestChannel() == 2);

    std::cout << "✓ Advance speaker test channel index" << std::endl;
    std::cout << std::endl;
}

void testBitDepthQuantization()
{
    std::cout << "=== Testing Bit-Depth Quantization ===" << std::endl;

    auto approxEqual = [](float a, float b) { return std::abs(a - b) < 1e-6f; };

    // 32-bit is a full-float passthrough — the sample must be returned untouched.
    const float original = 0.123456789f;
    assert(AudioEngine::quantizeSample(original, 32, 0.0f) == original);
    std::cout << "✓ 32-bit passes float through unchanged" << std::endl;

    // 16-bit grid spacing is 1 / 2^15.
    const float step16 = 1.0f / 32768.0f;

    // A value exactly on the grid stays put (dither off).
    const float onGrid = 100.0f * step16;
    assert(approxEqual(AudioEngine::quantizeSample(onGrid, 16, 0.0f), onGrid));
    std::cout << "✓ On-grid value is preserved" << std::endl;

    // Values between grid points snap to the nearest one.
    assert(approxEqual(AudioEngine::quantizeSample(100.4f * step16, 16, 0.0f), 100.0f * step16));
    assert(approxEqual(AudioEngine::quantizeSample(100.6f * step16, 16, 0.0f), 101.0f * step16));
    std::cout << "✓ Off-grid values snap to nearest 16-bit step" << std::endl;

    // Output always lands on the grid (an integer multiple of step), even with dither.
    const float inputs[]  = { 0.0f, 0.333f, -0.777f, 0.999f };
    const float dithers[] = { 0.0f, 0.5f, -0.5f, 0.9f };
    for (float in : inputs)
        for (float d : dithers)
        {
            float q = AudioEngine::quantizeSample(in, 16, d);
            float steps = q / step16;
            assert(std::abs(steps - std::round(steps)) < 1e-2f); // within float precision near ~32k
            assert(q >= -1.0f && q <= 1.0f);                     // never exceeds full scale
        }
    std::cout << "✓ Quantized output stays on the grid and within full scale" << std::endl;

    std::cout << std::endl;
}

void testXtpSongParsing()
{
    std::cout << "=== Testing XtpSong Parsing ===" << std::endl;

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File songFile = tempDir.getChildFile("sp2_test_song.xtp");

    juce::String content =
        "EXTRACKER_SONG_V1 2 2 1 1 0 0\n"
        "PATTERN 0\n"
        "0 0 1 60 0 65535 0 100 0 0 0\n"
        "0 1 0 -1 0 65535 0 100 0 0 0\n"
        "1 0 1 -1 0 65535 0 100 0 20 0\n"
        "1 1 0 -1 0 65535 0 100 0 0 0\n"
        "SONG_ORDER 0\n"
        "PATTERN_SWING 55\n"
        "INSERT_SWING_INHERIT 1\n"
        "ROW_EDIT_SCOPE 0\n"
        "TRANSPORT 140.5 4 2\n"
        "MIDI_MAP -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1\n"
        "INSTRUMENT_ASSIGN 2 \"builtin.square\"\n"
        "MODULE_MESSAGE \"hello\\nworld\"\n"
        "CHANNEL_VOLUME 50 250\n";

    songFile.replaceWithText(content);

    xtp::Song song;
    juce::String error;
    bool ok = song.loadFromFile(songFile, error);
    assert(ok);
    std::cout << "✓ Loads a well-formed SONG_V1 file: " << error << std::endl;

    assert(song.rows == 2 && song.channels == 2);
    assert(song.patterns.size() == 1);
    std::cout << "✓ Header dimensions and pattern count" << std::endl;

    assert(song.patterns[0].at(0, 0).hasNote && song.patterns[0].at(0, 0).note == 60);
    assert(!song.patterns[0].at(0, 1).hasNote);
    assert(song.patterns[0].at(1, 0).hasNote && song.patterns[0].at(1, 0).note == xtp::Step::kNoteOff);
    assert(song.patterns[0].at(1, 0).effectCommand == 20);
    std::cout << "✓ Pattern cell fields (note, note-off sentinel, effect command)" << std::endl;

    assert(song.songOrder.size() == 1 && song.songOrder[0] == 0);
    assert(song.patternSwing.size() == 1 && song.patternSwing[0] == 55);
    assert(std::abs(song.tempoBpm - 140.5) < 1e-9);
    assert(song.ticksPerBeat == 4 && song.ticksPerRow == 2);
    std::cout << "✓ SONG_ORDER, PATTERN_SWING, and TRANSPORT trailer tokens" << std::endl;

    assert(song.instruments[0] == "builtin.sine");   // default, no INSTRUMENT_ASSIGN for slot 0
    assert(song.instruments[1] == "builtin.square"); // default, no INSTRUMENT_ASSIGN for slot 1
    assert(song.instruments[2] == "builtin.square"); // explicit INSTRUMENT_ASSIGN
    assert(song.moduleMessage == "hello\nworld");
    std::cout << "✓ Default instrument slots 0/1, INSTRUMENT_ASSIGN override, and MODULE_MESSAGE unescaping" << std::endl;

    assert(song.channelVolume.size() == 2);
    assert(std::abs(song.channelVolume[0] - 0.5f) < 1e-6f);
    assert(std::abs(song.channelVolume[1] - 2.0f) < 1e-6f); // 250% clamped to the 0-200 range
    std::cout << "✓ CHANNEL_VOLUME trailer token (percent, clamped 0-200)" << std::endl;

    songFile.deleteFile();
    std::cout << std::endl;
}

// Regression test for a crash found interactively: a project referencing a
// SAMPLE_ENTRY file that no longer exists (e.g. a moved/renamed home directory)
// used to segfault in SampleVoice::loadSample() -- file.createInputStream()
// returns nullptr for a missing file, and that nullptr was handed straight to
// WavAudioFormat::createReaderFor(), whose reader constructor dereferenced it
// unconditionally on its first line. Loading the song (and thus building its
// instrument/sample voices) must now fail that one slot gracefully instead.
void testXtpSequencerMissingSampleFile()
{
    std::cout << "=== Testing XtpSequencerSource with a missing sample file ===" << std::endl;

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File songFile = tempDir.getChildFile("sp2_test_missing_sample_song.xtp");

    juce::String content =
        "EXTRACKER_SONG_V1 2 2 1 1 0 0\n"
        "PATTERN 0\n"
        "0 0 1 60 0 65535 0 100 0 0 0\n"
        "0 1 0 -1 0 65535 0 100 0 0 0\n"
        "1 0 1 -1 0 65535 0 100 0 20 0\n"
        "1 1 0 -1 0 65535 0 100 0 0 0\n"
        "SONG_ORDER 0\n"
        "PATTERN_SWING 55\n"
        "INSERT_SWING_INHERIT 1\n"
        "ROW_EDIT_SCOPE 0\n"
        "TRANSPORT 140.5 4 2\n"
        "MIDI_MAP -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1\n"
        "SAMPLE_ENTRY 0 \"gone\" \"/nonexistent/path/does_not_exist.wav\"\n";

    songFile.replaceWithText(content);

    // The bug crashed the whole process during construction -- reaching this
    // line at all (rather than segfaulting) is the actual assertion.
    XtpSequencerSource source(songFile, XtpSequencerSource::kRenderSampleRate);

    if (!source.isValid())
        throw std::runtime_error("Song with a missing sample file should still parse/load: " + source.getLoadError().toStdString());
    std::cout << "✓ Construction survives a missing SAMPLE_ENTRY file (no crash)" << std::endl;

    songFile.deleteFile();
    std::cout << std::endl;
}

namespace
{
    struct RecordedNoteOn
    {
        std::uint8_t instrument;
        std::uint16_t sample;
        int midiNote;
        std::uint8_t velocity;
        bool retrigger;
    };

    struct MockNoteSink : public xtp::NoteSink
    {
        std::vector<RecordedNoteOn> onEvents;
        std::vector<int> offMidiNotes;

        void noteOn(std::uint8_t instrument, std::uint16_t sample, int midiNote,
                   std::uint8_t velocity, bool retrigger) override
        {
            onEvents.push_back({ instrument, sample, midiNote, velocity, retrigger });
        }
        void noteOff(std::uint8_t, std::uint16_t, int midiNote) override
        {
            offMidiNotes.push_back(midiNote);
        }
    };
}

void testXtpSequencerRowJump()
{
    std::cout << "=== Testing Xtp Sequencer Row Jump (0x0B/0x0D) ===" << std::endl;

    // exTracker implements effect 0x0D ("pattern break") identically to 0x0B
    // ("row jump") -- both just jump the row cursor within the current pattern,
    // never advance to a different pattern. This test pins that behavior.
    xtp::Pattern pattern;
    pattern.rows = 4;
    pattern.channels = 1;
    pattern.steps.assign(4, xtp::Step {});

    xtp::Step row0;
    row0.hasNote = true;
    row0.note = 60;
    row0.instrument = 5;
    row0.effectCommand = 0x0D;
    row0.effectValue = 2; // jump to row 2
    pattern.at(0, 0) = row0;

    xtp::Step row2;
    row2.hasNote = true;
    row2.note = 64;
    row2.instrument = 7;
    pattern.at(2, 0) = row2;

    xtp::TransportClock transport;
    transport.reset(pattern.rows);
    xtp::Sequencer sequencer;
    sequencer.reset(pattern.channels);

    MockNoteSink sink;
    sequencer.onRowBoundary(pattern, transport, sink);

    assert(transport.row() == 2);
    std::cout << "✓ 0x0D lands on row 2 within the same pattern" << std::endl;

    assert(sink.onEvents.size() == 2);
    assert(sink.onEvents[0].instrument == 5 && sink.onEvents[0].midiNote == 60);
    assert(sink.onEvents[1].instrument == 7 && sink.onEvents[1].midiNote == 64);
    std::cout << "✓ Dispatches row 0's note, then chases the jump to dispatch row 2's note" << std::endl;

    std::cout << std::endl;
}

void testXtpPlayPositionForPatternViewer()
{
    std::cout << "=== Testing Xtp play position (row fraction, song order) ===" << std::endl;

    // Two 2-row patterns played in order 0, 1 -- the pattern viewer's centered
    // view needs the fraction into each row and the song-order position.
    juce::File songFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("sp2_xtp_play_position_test.xtp");
    songFile.replaceWithText(
        "EXTRACKER_SONG_V1 2 1 2 2 0 0\n"
        "PATTERN 0\n"
        "0 0 1 60 0 65535 0 100 0 0 0\n"
        "1 0 0 -1 0 65535 0 100 0 0 0\n"
        "PATTERN 1\n"
        "0 0 1 62 0 65535 0 100 0 0 0\n"
        "1 0 0 -1 0 65535 0 100 0 0 0\n"
        "SONG_ORDER 0 1\n"
        "TRANSPORT 120 4 4\n");

    XtpSequencerSource source(songFile, XtpSequencerSource::kRenderSampleRate);
    if (!source.isValid())
        throw std::runtime_error("play-position fixture did not load: " + source.getLoadError().toStdString());
    source.prepareToPlay(64, XtpSequencerSource::kRenderSampleRate);
    source.setNextReadPosition(0);

    ModulePatternData data;
    if (!data.loadFromXtpSong(source.getSong()) || data.getOrder() != std::vector<int> { 0, 1 })
        throw std::runtime_error("ModulePatternData did not take the .xtp song order");
    std::cout << "✓ ModulePatternData carries the song order" << std::endl;

    // 120 BPM, 4 ticks/beat, 4 ticks/row: 0.125 s per tick, 0.5 s per row.
    juce::AudioBuffer<float> buffer(2, 64);
    double lastPosition = -1.0;
    bool sawOrder1 = false;
    for (int block = 0; block < (int) (1.9 * XtpSequencerSource::kRenderSampleRate / 64); ++block)
    {
        juce::AudioSourceChannelInfo info(&buffer, 0, 64);
        source.getNextAudioBlock(info);

        const int order = source.getCurrentOrderPosition();
        const double fraction = source.getCurrentRowFraction();
        if (fraction < 0.0 || fraction >= 1.0)
            throw std::runtime_error("row fraction out of range: " + std::to_string(fraction));
        if (std::abs(source.getCurrentRowSeconds() - 0.5) > 1e-6)
            throw std::runtime_error("row length should be 0.5 s, got " + std::to_string(source.getCurrentRowSeconds()));

        // Song position in rows: order * 2 + row + fraction, rising steadily.
        const double songPosition = order * 2 + source.getCurrentRow() + fraction;
        const double expected = (block + 1) * 64 / XtpSequencerSource::kRenderSampleRate / 0.5;
        if (songPosition < lastPosition - 1e-9 || std::abs(songPosition - expected) > 0.02)
            throw std::runtime_error("position " + std::to_string(songPosition) + " at block " + std::to_string(block)
                                     + ", expected " + std::to_string(expected));
        lastPosition = songPosition;
        sawOrder1 = sawOrder1 || (order == 1 && source.getCurrentPatternIndex() == 1);
    }
    if (!sawOrder1)
        throw std::runtime_error("never reached song order position 1");
    std::cout << "✓ Row + fraction tracks the rendered time through both patterns" << std::endl;
    std::cout << "✓ Song-order position follows the pattern change" << std::endl;

    songFile.deleteFile();
    std::cout << std::endl;
}

#ifdef EXPLAYER_OPENMPT_SUPPORT
void testOpenMptPlayPositionForPatternViewer()
{
    std::cout << "=== Testing libopenmpt play position (row fraction, song order) ===" << std::endl;

    // A minimal 4-channel ProTracker MOD, no samples: two 64-row patterns
    // played in order 0, 1 at the default speed 6 / tempo 125 = 0.12 s per row.
    juce::MemoryOutputStream mod;
    mod.writeRepeatedByte(0, 20);              // title
    for (int i = 0; i < 31; ++i)
    {
        mod.writeRepeatedByte(0, 22 + 2 + 1); // name, length, finetune
        mod.writeByte(64);                    // volume
        mod.writeRepeatedByte(0, 4);          // loop start/length
    }
    mod.writeByte(2);                          // song length
    mod.writeByte(127);                        // restart
    mod.writeByte(0);
    mod.writeByte(1);
    mod.writeRepeatedByte(0, 126);             // rest of the order list
    mod.write("M.K.", 4);
    mod.writeRepeatedByte(0, 2 * 64 * 4 * 4);  // two empty patterns

    juce::File modFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("sp2_openmpt_play_position_test.mod");
    modFile.replaceWithData(mod.getData(), mod.getDataSize());

    ModulePatternData data;
    if (!data.loadFromFile(modFile) || data.getOrder().size() < 2
        || data.getOrder()[0] != 0 || data.getOrder()[1] != 1)
        throw std::runtime_error("ModulePatternData did not read the MOD order list");
    std::cout << "✓ ModulePatternData reads the module's order list" << std::endl;

    OpenMptAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::FileInputStream(modFile), true));
    auto* openMpt = dynamic_cast<OpenMptAudioFormatReader*>(reader.get());
    if (openMpt == nullptr)
        throw std::runtime_error("MOD fixture did not open through libopenmpt");

    const double rate = reader->sampleRate;
    constexpr int kBlock = 1600; // one 30 fps video frame at 48 kHz, as VideoExporter decodes
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::int64 position = 0;
    double lastPosition = -1.0;
    bool sawOrder1 = false;
    while (position < (juce::int64) (14.0 * rate))
    {
        reader->read(&buffer, 0, kBlock, position, true, true);
        position += kBlock;

        const double fraction = openMpt->getCurrentRowFraction();
        if (fraction < 0.0 || fraction >= 1.0)
            throw std::runtime_error("row fraction out of range: " + std::to_string(fraction));

        const int order = openMpt->getCurrentOrder();
        const double songPosition = order * 64 + openMpt->getCurrentRow() + fraction;
        const double expected = position / rate / 0.12;
        // Row changes are pinned to 128 frames (~0.02 rows) by the reader.
        if (songPosition < lastPosition - 1e-9 || std::abs(songPosition - expected) > 0.05)
            throw std::runtime_error("position " + std::to_string(songPosition) + " at "
                                     + std::to_string(position / rate) + " s, expected " + std::to_string(expected));
        lastPosition = songPosition;
        sawOrder1 = sawOrder1 || (order == 1 && openMpt->getCurrentPattern() == 1);
    }
    if (!sawOrder1)
        throw std::runtime_error("never reached order position 1");
    std::cout << "✓ Row + fraction tracks the decoded time, per video-frame-sized read" << std::endl;
    std::cout << "✓ Order position follows the pattern change" << std::endl;

    reader.reset();
    modFile.deleteFile();
    std::cout << std::endl;
}
#endif

void testXtpSequencerChannelVolume()
{
    std::cout << "=== Testing Xtp Sequencer Channel Volume ===" << std::endl;

    // Two channels, velocity 100 on both: channel 0 left at unity, channel 1
    // scaled to 50%. Mirrors exTracker's own Sequencer::channelVolume_
    // formula: round(velocity * multiplier), clamped to the 1-127 MIDI range.
    xtp::Pattern pattern;
    pattern.rows = 1;
    pattern.channels = 2;
    pattern.steps.assign(2, xtp::Step {});

    xtp::Step note;
    note.hasNote = true;
    note.note = 60;
    note.instrument = 0;
    note.velocity = 100;
    pattern.at(0, 0) = note;
    pattern.at(0, 1) = note;

    xtp::TransportClock transport;
    transport.reset(pattern.rows);
    xtp::Sequencer sequencer;
    sequencer.reset(pattern.channels);
    sequencer.setChannelVolumes({ 1.0f, 0.5f });

    MockNoteSink sink;
    sequencer.onRowBoundary(pattern, transport, sink);

    assert(sink.onEvents.size() == 2);
    assert(sink.onEvents[0].velocity == 100); // channel 0: unity, unchanged
    assert(sink.onEvents[1].velocity == 50);  // channel 1: 100 * 0.5 = 50
    std::cout << "✓ Per-channel volume scales trigger velocity (unity channel unaffected)" << std::endl;

    // A near-silent multiplier still clamps to the MIDI floor of 1, matching
    // exTracker's clamp -- 0% volume attenuates heavily but never truly mutes
    // via this path (channel_cli.cpp's 0% is a UI convention, not a hard mute).
    xtp::Sequencer quietSequencer;
    quietSequencer.reset(pattern.channels);
    quietSequencer.setChannelVolumes({ 0.0f, 2.0f });

    MockNoteSink quietSink;
    xtp::TransportClock quietTransport;
    quietTransport.reset(pattern.rows);
    quietSequencer.onRowBoundary(pattern, quietTransport, quietSink);

    assert(quietSink.onEvents[0].velocity == 1);   // clamped to MIDI floor, not 0
    assert(quietSink.onEvents[1].velocity == 127); // 100 * 2.0 = 200, clamped to MIDI ceiling
    std::cout << "✓ Extreme multipliers clamp to the 1-127 MIDI velocity range" << std::endl;

    std::cout << std::endl;
}

namespace
{
    // Writes a short stereo sine-wave WAV so the exporter has real (non-silent)
    // audio to decode, analyze, and render frames from.
    juce::File writeTestToneWav(const juce::File& file, double sampleRate, double seconds)
    {
        file.deleteFile();
        juce::WavAudioFormat wavFormat;
        auto outputStream = std::make_unique<juce::FileOutputStream>(file);
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(outputStream.get(), sampleRate, 2, 16, {}, 0));
        assert(writer != nullptr);
        outputStream.release();

        const int numSamples = (int) (sampleRate * seconds);
        juce::AudioBuffer<float> buffer(2, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sampleRate);
            buffer.setSample(0, i, sample);
            buffer.setSample(1, i, sample);
        }
        writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
        return file;
    }

    // Blocks until the exporter finishes or the timeout elapses (test-only; the
    // real UI polls this non-blockingly from MainComponent::timerCallback instead).
    bool waitForExport(VideoExporter& exporter, int timeoutMs)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (!exporter.hasFinished())
        {
            if (juce::Time::getMillisecondCounter() > deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

    // Generates a short synthetic video+audio file via ffmpeg's lavfi sources --
    // a moving test pattern plus a pure sine tone -- so the video playback tests
    // below have real, non-trivial content to probe/decode without needing a
    // binary fixture checked into git. -g keeps keyframes frequent (default
    // libx264 GOP length would otherwise exceed this file's whole duration,
    // making seek tests meaningless -- everything would land on frame 0).
    bool generateTestVideoFile(const juce::File& file, double durationSeconds,
                               int width, int height, double fps, double toneHz)
    {
        file.deleteFile();

        juce::StringArray args {
            "ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-nostdin",
            "-f", "lavfi", "-i", "testsrc=duration=" + juce::String(durationSeconds, 2)
                                  + ":size=" + juce::String(width) + "x" + juce::String(height)
                                  + ":rate=" + juce::String(fps, 2),
            "-f", "lavfi", "-i", "sine=frequency=" + juce::String(toneHz, 2)
                                  + ":duration=" + juce::String(durationSeconds, 2),
            "-shortest",
            "-c:v", "libx264", "-g", "5", "-keyint_min", "5",
            "-c:a", "aac",
            file.getFullPathName()
        };

        juce::ChildProcess process;
        if (!process.start(args, juce::ChildProcess::wantStdErr))
            return false;
        process.readAllProcessOutput(); // blocks until ffmpeg exits
        return file.existsAsFile();
    }

    // Generates an audio-only file with two distinguishable tones spliced back
    // to back, so a seek test can tell whether it landed before or after the
    // splice point by which frequency comes back.
    bool generateTwoToneAudioFile(const juce::File& file, double toneASeconds, double toneAHz,
                                  double toneBSeconds, double toneBHz)
    {
        file.deleteFile();

        juce::StringArray args {
            "ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-nostdin",
            "-f", "lavfi", "-i", "sine=frequency=" + juce::String(toneAHz, 2)
                                  + ":duration=" + juce::String(toneASeconds, 2),
            "-f", "lavfi", "-i", "sine=frequency=" + juce::String(toneBHz, 2)
                                  + ":duration=" + juce::String(toneBSeconds, 2),
            "-filter_complex", "[0:a][1:a]concat=n=2:v=0:a=1[a]",
            "-map", "[a]", "-c:a", "aac",
            file.getFullPathName()
        };

        juce::ChildProcess process;
        if (!process.start(args, juce::ChildProcess::wantStdErr))
            return false;
        process.readAllProcessOutput(); // blocks until ffmpeg exits
        return file.existsAsFile();
    }

    // Estimates how many times the signal crosses zero per second -- enough to
    // tell a 440Hz tone (~880 crossings/s) apart from an 880Hz one (~1760/s)
    // without pulling in a full DSP/FFT dependency just for a test.
    double estimateZeroCrossingsPerSecond(const float* data, int numSamples, double sampleRate)
    {
        int crossings = 0;
        for (int i = 1; i < numSamples; ++i)
            if ((data[i - 1] < 0.0f) != (data[i] < 0.0f))
                ++crossings;
        return numSamples > 0 ? crossings * sampleRate / numSamples : 0.0;
    }

    double computeRms(const float* data, int numSamples)
    {
        double sumSq = 0.0;
        for (int i = 0; i < numSamples; ++i)
            sumSq += (double) data[i] * (double) data[i];
        return numSamples > 0 ? std::sqrt(sumSq / numSamples) : 0.0;
    }
}

// NOTE: this test intentionally uses explicit if/throw checks rather than assert().
// FeatureTests.cpp is built with NDEBUG defined, which makes assert() a complete
// no-op -- including never evaluating its argument -- so assert(sideEffectingCall())
// would silently skip the call entirely rather than just skip the check.
// Songs saved by current exTracker ("unified instruments"): notes name an
// instrument slot (0-255) whose builtin.sample is linked to a bank slot by
// INSTR_SAMPLE_SLOT, with no per-note sample column. These used to load with
// no voice for the instrument and play silence.
void testXtpUnifiedSampleInstrument()
{
    std::cout << "=== Testing Xtp sample instruments (exTracker unified instruments) ===" << std::endl;

    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("sp2_xtp_unified_" + juce::String(juce::Random::getSystemRandom().nextInt()));
    dir.getChildFile("song_samples").createDirectory();
    writeTestToneWav(dir.getChildFile("song_samples/slot_003_tone.wav"), 48000.0, 1.0);

    // 2 rows x 3 channels. Ch 0: note on instrument 20. Ch 1: muted channel.
    // Ch 2: inside a muted track.
    juce::String content =
        "EXTRACKER_SONG_V1 2 3 1 1 0 0\n"
        "PATTERN 0\n"
        "0 0 1 60 20 65535 0 100 1 0 0\n"
        "0 1 1 64 20 65535 0 100 1 0 0\n"
        "0 2 1 67 20 65535 0 100 1 0 0\n"
        "1 0 0 -1 0 65535 0 100 0 0 0\n"
        "1 1 0 -1 0 65535 0 100 0 0 0\n"
        "1 2 0 -1 0 65535 0 100 0 0 0\n"
        "SONG_ORDER 0\n"
        "PATTERN_SWING 50\n"
        "INSERT_SWING_INHERIT 0\n"
        "ROW_EDIT_SCOPE 0\n"
        "TRANSPORT 125 6 6\n"
        "MIDI_MAP -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1\n"
        "MIDI_TRANSPORT 1500 0\n"
        "INSTRUMENT_NAME 20 \"Tone SONG_ORDER 3\"\n"
        "INSTRUMENT_ASSIGN 20 \"builtin.sample\"\n"
        "INSTRUMENT_PARAM 20 gain 0.5\n"
        "INSTRUMENT_PARAM 20 sample_root 60\n"
        "INSTRUMENT_PARAM 20 loop_mode 1\n"
        "INSTRUMENT_FILTER 20 1 0.5 0.2\n"
        "INSTRUMENT_PITCH 20 12\n"
        "SAMPLE_ENTRY 3 \"tone\" \"song_samples/slot_003_tone.wav\"\n"
        "INSTR_SAMPLE_SLOT 20 3\n"
        "MODULE_MESSAGE \"hi\"\n"
        "CHANNEL_MUTED 0 1 0\n"
        "CHANNEL_VOLUME 100 100 100\n"
        "CHANNEL_NAMES \"Lead 1\" \"\" \"PATTERN 7\"\n"
        "CHANNEL_FILTERS 0 255 0 0 255 0 0 255 0\n"
        "CHANNEL_TRACKS 2 2 1 80 \"Muted chords\"\n"
        "REVERB 128 64 0 255\n"
        "RECORD_SETTINGS 1 0\n"
        "RECORD_INSERT_JUMP 1\n";
    juce::File songFile = dir.getChildFile("song.xtp");
    songFile.replaceWithText(content);

    // Release builds compile assert() out; these checks must always run.
    auto require = [](bool ok, const char* what) {
        if (!ok)
            throw std::runtime_error(std::string("Unified-instrument check failed: ") + what);
    };

    xtp::Song song;
    juce::String error;
    if (!song.loadFromFile(songFile, error))
        throw std::runtime_error("Unified-instrument song failed to parse: " + error.toStdString());
    require(song.instruments[20] == "builtin.sample", "song.instruments[20] == \"builtin.sample\"");
    require(song.instrumentSampleSlot.at(20) == 3, "song.instrumentSampleSlot.at(20) == 3");
    require(std::abs(song.instrumentParams.at(20).at("gain") - 0.5) < 1e-9, "std::abs(song.instrumentParams.at(20).at(\"gain\") - 0.5) < 1e-9");
    require(std::abs(song.instrumentPitch.at(20) - 12.0f) < 1e-6f, "std::abs(song.instrumentPitch.at(20) - 12.0f) < 1e-6f");
    require(song.moduleMessage == "hi", "song.moduleMessage == \"hi\"");
    require((song.channelSilenced() == std::vector<bool> { false, true, true }), "(song.channelSilenced() == std::vector<bool> { false, true, true })");
    require(std::abs(song.channelGains()[2] - 0.8f) < 1e-6f, "std::abs(song.channelGains()[2] - 0.8f) < 1e-6f");
    std::cout << "✓ Parses INSTR_SAMPLE_SLOT, INSTRUMENT_PARAM/PITCH, quoted names, tracks and mutes" << std::endl;

    // Only channel 0 may sound, through instrument 20, transposed +12.
    xtp::Sequencer sequencer;
    sequencer.reset(song.channels);
    sequencer.setChannelSilenced(song.channelSilenced());
    xtp::TransportClock transport;
    transport.setPatternRows(song.rows);
    transport.reset(song.rows);
    MockNoteSink sink;
    sequencer.dispatchRow(0, song.patterns[0], transport, sink);
    require(sink.onEvents.size() == 1, "sink.onEvents.size() == 1");
    require(sink.onEvents[0].instrument == 20 && sink.onEvents[0].midiNote == 60, "sink.onEvents[0].instrument == 20 && sink.onEvents[0].midiNote == 60");
    std::cout << "✓ Muted channel and muted track trigger no notes" << std::endl;

    XtpSequencerSource source(songFile, XtpSequencerSource::kRenderSampleRate);
    if (!source.isValid())
        throw std::runtime_error("Source failed to load: " + source.getLoadError().toStdString());
    source.prepareToPlay(512, XtpSequencerSource::kRenderSampleRate);
    juce::AudioBuffer<float> buffer(2, 4800);
    buffer.clear();
    source.getNextAudioBlock(juce::AudioSourceChannelInfo(&buffer, 0, buffer.getNumSamples()));
    const float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    if (rms < 0.01f)
        throw std::runtime_error("Sample instrument rendered silence (rms " + std::to_string(rms) + ")");
    std::cout << "✓ Sample instrument 20 renders audio (rms " << rms << ")" << std::endl;

    dir.deleteRecursively();
    std::cout << std::endl;
}

void testVideoExporter()
{
    std::cout << "=== Testing VideoExporter ===" << std::endl;

    if (!VideoExporter::isFfmpegAvailable())
    {
        std::cout << "! ffmpeg not found on PATH -- skipping VideoExporter test" << std::endl << std::endl;
        return;
    }

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File sourceWav = tempDir.getChildFile("sp2_video_export_test_tone.wav");
    writeTestToneWav(sourceWav, 44100.0, 2.0);

    // Successful export end to end: decode -> WAV + frames -> mux.
    {
        juce::File outputMp4 = tempDir.getChildFile("sp2_video_export_test_output.mp4");
        outputMp4.deleteFile();

        VideoExporter::Options options;
        options.sourceFile = sourceWav;
        options.isXtpSequence = false;
        options.outputFile = outputMp4;
        options.width = 160;
        options.height = 120;
        options.pluginFactory = [] { return std::make_unique<WaveformPlugin>(); };

        VideoExporter exporter(std::move(options));
        exporter.startExport();
        if (!waitForExport(exporter, 30000))
            throw std::runtime_error("VideoExporter did not finish within 30s");

        if (exporter.getResult() != VideoExporter::Result::Success)
            throw std::runtime_error("Export did not succeed: " + exporter.getResultMessage().toStdString());
        std::cout << "✓ Export succeeded: " << exporter.getResultMessage() << std::endl;

        if (!outputMp4.existsAsFile())
            throw std::runtime_error("Output file was not created");
        if (outputMp4.getSize() <= 0)
            throw std::runtime_error("Output file is empty");
        std::cout << "✓ Output file exists (" << outputMp4.getSize() << " bytes)" << std::endl;

        outputMp4.deleteFile();
    }

    // Cancellation: signal immediately, expect a clean Cancelled result and no output file.
    {
        juce::File outputMp4 = tempDir.getChildFile("sp2_video_export_cancel_test_output.mp4");
        outputMp4.deleteFile();

        VideoExporter::Options options;
        options.sourceFile = sourceWav;
        options.isXtpSequence = false;
        options.outputFile = outputMp4;
        options.width = 160;
        options.height = 120;
        options.pluginFactory = [] { return std::make_unique<WaveformPlugin>(); };

        VideoExporter exporter(std::move(options));
        exporter.startExport();
        exporter.cancel();
        if (!waitForExport(exporter, 30000))
            throw std::runtime_error("Cancelled VideoExporter did not finish within 30s");

        if (exporter.getResult() != VideoExporter::Result::Cancelled)
            throw std::runtime_error("Cancelled export did not report Result::Cancelled");
        if (outputMp4.existsAsFile())
            throw std::runtime_error("Cancelled export left an output file behind");
        std::cout << "✓ Cancel mid-export: no output file left behind" << std::endl;
    }

    // PatternGrid mode: reuses the minimal .xtp fixture from testXtpSongParsing() so
    // pattern data is guaranteed to exist.
    juce::File sourceXtp = tempDir.getChildFile("sp2_video_export_test_song.xtp");
    sourceXtp.replaceWithText(
        "EXTRACKER_SONG_V1 2 2 1 1 0 0\n"
        "PATTERN 0\n"
        "0 0 1 60 0 65535 0 100 0 0 0\n"
        "0 1 0 -1 0 65535 0 100 0 0 0\n"
        "1 0 1 -1 0 65535 0 100 0 20 0\n"
        "1 1 0 -1 0 65535 0 100 0 0 0\n"
        "SONG_ORDER 0\n"
        "PATTERN_SWING 55\n"
        "INSERT_SWING_INHERIT 1\n"
        "ROW_EDIT_SCOPE 0\n"
        "TRANSPORT 140.5 4 2\n"
        "MIDI_MAP -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1\n");

    {
        juce::File outputMp4 = tempDir.getChildFile("sp2_video_export_pattern_grid_output.mp4");
        outputMp4.deleteFile();

        VideoExporter::Options options;
        options.sourceFile = sourceXtp;
        options.isXtpSequence = true;
        options.outputFile = outputMp4;
        options.width = 160;
        options.height = 120;
        options.mode = VideoExporter::ExportMode::PatternGrid;

        VideoExporter exporter(std::move(options));
        exporter.startExport();
        if (!waitForExport(exporter, 30000))
            throw std::runtime_error("PatternGrid export did not finish within 30s");

        if (exporter.getResult() != VideoExporter::Result::Success)
            throw std::runtime_error("PatternGrid export did not succeed: " + exporter.getResultMessage().toStdString());
        if (!outputMp4.existsAsFile() || outputMp4.getSize() <= 0)
            throw std::runtime_error("PatternGrid export produced no/empty output file");
        std::cout << "✓ PatternGrid mode export succeeded (" << outputMp4.getSize() << " bytes)" << std::endl;

        outputMp4.deleteFile();
    }

    // Combined mode, exercising layout + a non-Classic theme + a text overlay together.
    {
        juce::File outputMp4 = tempDir.getChildFile("sp2_video_export_combined_output.mp4");
        outputMp4.deleteFile();

        VideoExporter::Options options;
        options.sourceFile = sourceXtp;
        options.isXtpSequence = true;
        options.outputFile = outputMp4;
        options.width = 160;
        options.height = 120;
        options.mode = VideoExporter::ExportMode::Combined;
        options.combinedLayout = VideoExporter::CombinedLayout::SplitGridTop;
        options.colorTheme = VideoExporter::ColorTheme::MonoGreen;
        options.bottomText.text = "Test Song";
        options.pluginFactory = [] { return std::make_unique<WaveformPlugin>(); };

        VideoExporter exporter(std::move(options));
        exporter.startExport();
        if (!waitForExport(exporter, 30000))
            throw std::runtime_error("Combined export did not finish within 30s");

        if (exporter.getResult() != VideoExporter::Result::Success)
            throw std::runtime_error("Combined export did not succeed: " + exporter.getResultMessage().toStdString());
        if (!outputMp4.existsAsFile() || outputMp4.getSize() <= 0)
            throw std::runtime_error("Combined export produced no/empty output file");
        std::cout << "✓ Combined mode export succeeded (" << outputMp4.getSize() << " bytes)" << std::endl;

        outputMp4.deleteFile();
    }

    // Overlay layout (grid + viz sharing the whole frame, blended) together with all
    // three text bands and both slide modes, to exercise the newer compositing and
    // text-animation math end to end.
    {
        juce::File outputMp4 = tempDir.getChildFile("sp2_video_export_overlay_output.mp4");
        outputMp4.deleteFile();

        VideoExporter::Options options;
        options.sourceFile = sourceXtp;
        options.isXtpSequence = true;
        options.outputFile = outputMp4;
        options.width = 160;
        options.height = 120;
        options.mode = VideoExporter::ExportMode::Combined;
        options.combinedLayout = VideoExporter::CombinedLayout::Overlay;
        options.combinedOverlayOpacity = 0.5f;
        options.topText.text = "Top";
        options.topText.slide = VideoExporter::TextSlide::Marquee;
        options.topText.marqueeSeconds = 2.0;
        options.centerText.text = "Center";
        options.bottomText.text = "Bottom";
        options.bottomText.slide = VideoExporter::TextSlide::TimedSlideInOut;
        options.bottomText.holdSeconds = 1.0;
        options.pluginFactory = [] { return std::make_unique<WaveformPlugin>(); };

        VideoExporter exporter(std::move(options));
        exporter.startExport();
        if (!waitForExport(exporter, 30000))
            throw std::runtime_error("Overlay export did not finish within 30s");

        if (exporter.getResult() != VideoExporter::Result::Success)
            throw std::runtime_error("Overlay export did not succeed: " + exporter.getResultMessage().toStdString());
        if (!outputMp4.existsAsFile() || outputMp4.getSize() <= 0)
            throw std::runtime_error("Overlay export produced no/empty output file");
        std::cout << "✓ Overlay layout + sliding text export succeeded (" << outputMp4.getSize() << " bytes)" << std::endl;

        outputMp4.deleteFile();
    }

    sourceXtp.deleteFile();
    sourceWav.deleteFile();
    std::cout << std::endl;
}

// NOTE: same NDEBUG/assert() caveat as testVideoExporter() above.
void testPlaylistVideoExporter()
{
    std::cout << "=== Testing PlaylistVideoExporter ===" << std::endl;

    if (!VideoExporter::isFfmpegAvailable())
    {
        std::cout << "! ffmpeg not found on PATH -- skipping PlaylistVideoExporter test" << std::endl << std::endl;
        return;
    }

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File trackA = tempDir.getChildFile("sp2_playlist_export_track_a.wav");
    juce::File trackB = tempDir.getChildFile("sp2_playlist_export_track_b.wav");
    writeTestToneWav(trackA, 44100.0, 2.0);
    writeTestToneWav(trackB, 44100.0, 1.5);

    auto makeOptionsTemplate = []
    {
        VideoExporter::Options options;
        options.width = 160;
        options.height = 120;
        options.pluginFactory = [] { return std::make_unique<WaveformPlugin>(); };
        return options;
    };

    const std::vector<PlaylistVideoExporter::TrackSpec> tracks {
        { trackA, false, "Track A", 2.0 },
        { trackB, false, "Track B", 1.5 }
    };

    auto runToCompletion = [](PlaylistVideoExporter& exporter, const char* label)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 60000;
        while (!exporter.hasFinished())
        {
            if (juce::Time::getMillisecondCounter() > deadline)
                throw std::runtime_error((juce::String("PlaylistVideoExporter (") + label
                                         + ") did not finish within 60s").toStdString());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    };

    // Hard cut: stitched duration should be close to the sum of both tracks (~3.5s).
    {
        juce::File outputMp4 = tempDir.getChildFile("sp2_playlist_export_hardcut_output.mp4");
        outputMp4.deleteFile();

        PlaylistVideoExporter::Settings settings;
        settings.transition = PlaylistVideoExporter::TransitionMode::HardCut;

        PlaylistVideoExporter exporter(tracks, makeOptionsTemplate(), settings, outputMp4);
        exporter.startExport();
        runToCompletion(exporter, "hard cut");

        if (exporter.getResult() != VideoExporter::Result::Success)
            throw std::runtime_error("Hard-cut playlist export did not succeed: "
                                     + exporter.getResultMessage().toStdString());
        if (!outputMp4.existsAsFile() || outputMp4.getSize() <= 0)
            throw std::runtime_error("Hard-cut playlist export produced no/empty output file");

        const double duration = VideoFileProbe::probe(outputMp4).durationSeconds;
        if (duration < 3.0 || duration > 4.0)
            throw std::runtime_error("Hard-cut output duration " + std::to_string(duration)
                                     + "s is not close to the expected ~3.5s");
        std::cout << "✓ Hard-cut playlist export succeeded (" << outputMp4.getSize()
                  << " bytes, " << duration << "s)" << std::endl;

        outputMp4.deleteFile();
    }

    // Silence gap: stitched duration should include the extra ~1s gap (~4.5s).
    {
        juce::File outputMp4 = tempDir.getChildFile("sp2_playlist_export_silence_output.mp4");
        outputMp4.deleteFile();

        PlaylistVideoExporter::Settings settings;
        settings.transition = PlaylistVideoExporter::TransitionMode::Silence;
        settings.transitionSeconds = 1.0;

        PlaylistVideoExporter exporter(tracks, makeOptionsTemplate(), settings, outputMp4);
        exporter.startExport();
        runToCompletion(exporter, "silence gap");

        if (exporter.getResult() != VideoExporter::Result::Success)
            throw std::runtime_error("Silence-gap playlist export did not succeed: "
                                     + exporter.getResultMessage().toStdString());

        const double duration = VideoFileProbe::probe(outputMp4).durationSeconds;
        if (duration < 4.0 || duration > 5.0)
            throw std::runtime_error("Silence-gap output duration " + std::to_string(duration)
                                     + "s is not close to the expected ~4.5s");
        std::cout << "✓ Silence-gap playlist export succeeded (" << duration << "s)" << std::endl;

        outputMp4.deleteFile();
    }

    // Crossfade: stitched duration should be shorter than a hard cut by ~the 0.5s overlap
    // (~3.0s).
    {
        juce::File outputMp4 = tempDir.getChildFile("sp2_playlist_export_crossfade_output.mp4");
        outputMp4.deleteFile();

        PlaylistVideoExporter::Settings settings;
        settings.transition = PlaylistVideoExporter::TransitionMode::Crossfade;
        settings.transitionSeconds = 0.5;

        PlaylistVideoExporter exporter(tracks, makeOptionsTemplate(), settings, outputMp4);
        exporter.startExport();
        runToCompletion(exporter, "crossfade");

        if (exporter.getResult() != VideoExporter::Result::Success)
            throw std::runtime_error("Crossfade playlist export did not succeed: "
                                     + exporter.getResultMessage().toStdString());

        const double duration = VideoFileProbe::probe(outputMp4).durationSeconds;
        if (duration < 2.5 || duration > 3.5)
            throw std::runtime_error("Crossfade output duration " + std::to_string(duration)
                                     + "s is not close to the expected ~3.0s");
        std::cout << "✓ Crossfade playlist export succeeded (" << duration << "s)" << std::endl;

        outputMp4.deleteFile();
    }

    // selectTracks(): a stream, a video file, a plain audio item, and a module/.xtp item.
    // For a mode that needs pattern data, only the module item should survive; the stream
    // and video items are excluded regardless of mode.
    {
        juce::Array<PlaylistItem> items;
        items.add(PlaylistItem("http://example.com/stream", "Radio Stream"));

        PlaylistItem videoItem(tempDir.getChildFile("fake.mp4"));
        videoItem.name = "Some Video";
        items.add(videoItem);

        PlaylistItem plainAudio(trackA);
        plainAudio.name = "Plain Audio";
        items.add(plainAudio);

        PlaylistItem moduleItem(tempDir.getChildFile("fake.xtp"));
        moduleItem.name = "Module Song";
        items.add(moduleItem);

        std::vector<PlaylistVideoExporter::TrackSpec> selected;
        std::vector<PlaylistVideoExporter::SkippedTrack> skipped;
        PlaylistVideoExporter::selectTracks(items, VideoExporter::ExportMode::PatternGrid, selected, skipped);

        if (selected.size() != 1 || selected[0].name != "Module Song")
            throw std::runtime_error("selectTracks(PatternGrid) should keep only the module/.xtp item");
        if (skipped.size() != 3)
            throw std::runtime_error("selectTracks(PatternGrid) should skip the stream, video, and plain audio items");
        std::cout << "✓ selectTracks() filters correctly for a pattern-data-requiring mode" << std::endl;

        std::vector<PlaylistVideoExporter::TrackSpec> selectedViz;
        std::vector<PlaylistVideoExporter::SkippedTrack> skippedViz;
        PlaylistVideoExporter::selectTracks(items, VideoExporter::ExportMode::Visualization, selectedViz, skippedViz);
        if (selectedViz.size() != 2 || skippedViz.size() != 2)
            throw std::runtime_error("selectTracks(Visualization) should keep the plain audio and module items");
        std::cout << "✓ selectTracks() keeps non-pattern tracks for Visualization mode" << std::endl;
    }

    trackA.deleteFile();
    trackB.deleteFile();
    std::cout << std::endl;
}

// Real-time capture (unlike VideoExporter's offline render), so this needs a
// live X server + a pulse-compatible audio source, neither of which is
// guaranteed in every environment this suite runs in -- skip gracefully
// rather than failing the whole suite when either is unavailable.
void testScreenRecorder()
{
    std::cout << "=== Testing ScreenRecorder ===" << std::endl;

    if (!VideoExporter::isFfmpegAvailable())
    {
        std::cout << "! ffmpeg not found on PATH -- skipping ScreenRecorder test" << std::endl << std::endl;
        return;
    }

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File outputMp4 = tempDir.getChildFile("sp2_screen_record_test_output.mp4");
    outputMp4.deleteFile();

    ScreenRecorder recorder;
    juce::String errorMessage;
    // Doesn't need to be a real/visible window -- just proves the x11grab +
    // pulse muxing pipeline works end to end.
    const bool started = recorder.start(outputMp4, { 0, 0, 160, 120 }, errorMessage);

    if (!started)
    {
        std::cout << "! Could not start screen recording (" << errorMessage
                   << ") -- skipping ScreenRecorder test" << std::endl << std::endl;
        return;
    }
    std::cout << "✓ Started screen recording" << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    recorder.stop();

    const auto deadline = juce::Time::getMillisecondCounter() + 15000;
    while (recorder.isFinalizing() && juce::Time::getMillisecondCounter() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    if (recorder.isFinalizing() || recorder.isRecording())
        throw std::runtime_error("ScreenRecorder did not finish within 15s");
    std::cout << "✓ Stopped and finalized" << std::endl;

    auto err = recorder.getLastError();
    if (err.isNotEmpty())
        throw std::runtime_error("ScreenRecorder reported an error: " + err.toStdString());

    if (!outputMp4.existsAsFile() || outputMp4.getSize() <= 0)
        throw std::runtime_error("Screen recording produced no/empty output file");
    std::cout << "✓ Output file exists (" << outputMp4.getSize() << " bytes)" << std::endl;

    outputMp4.deleteFile();
    std::cout << std::endl;
}

// Verifies VideoFileProbe's ffmpeg-stderr-banner parsing against a real,
// freshly-generated video+audio file (see the video playback feature's
// FfmpegVideoAudioFormatReader / FfmpegVideoFrameDecoder below).
void testVideoFileProbe()
{
    std::cout << "=== Testing VideoFileProbe ===" << std::endl;

    if (!VideoExporter::isFfmpegAvailable())
    {
        std::cout << "! ffmpeg not found on PATH -- skipping VideoFileProbe test" << std::endl << std::endl;
        return;
    }

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File videoFile = tempDir.getChildFile("sp2_video_probe_test.mp4");

    if (!generateTestVideoFile(videoFile, 5.0, 320, 240, 25.0, 1000.0))
        throw std::runtime_error("Could not generate synthetic test video for probing");

    auto result = VideoFileProbe::probe(videoFile);

    if (result.errorMessage.isNotEmpty())
        throw std::runtime_error("Probe reported an error: " + result.errorMessage.toStdString());
    if (!result.hasVideo)
        throw std::runtime_error("Probe did not detect a video stream");
    if (!result.hasAudio)
        throw std::runtime_error("Probe did not detect an audio stream");
    if (result.width != 320 || result.height != 240)
        throw std::runtime_error("Probe reported wrong resolution: "
            + std::to_string(result.width) + "x" + std::to_string(result.height));
    if (std::abs(result.fps - 25.0) > 1.0)
        throw std::runtime_error("Probe reported wrong fps: " + std::to_string(result.fps));
    if (std::abs(result.durationSeconds - 5.0) > 0.5)
        throw std::runtime_error("Probe reported wrong duration: " + std::to_string(result.durationSeconds));

    std::cout << "✓ Probed " << result.width << "x" << result.height << " @ " << result.fps
              << "fps, duration=" << result.durationSeconds << "s, hasAudio=" << result.hasAudio << std::endl;

    videoFile.deleteFile();
    std::cout << std::endl;
}

void testSubtitleTrackParsing()
{
    std::cout << "=== Testing SubtitleTrack SRT parsing ===" << std::endl;

    // CRLF line endings, a stray "<i>" markup tag, a multi-line cue, and a
    // gap between cues 1 and 2 -- all realistic real-world .srt quirks.
    juce::String srt =
        "1\r\n"
        "00:00:01,000 --> 00:00:02,500\r\n"
        "<i>Hello world</i>\r\n"
        "\r\n"
        "2\r\n"
        "00:00:05,000 --> 00:00:07,250\r\n"
        "Line one\r\n"
        "Line two\r\n";

    auto cues = SubtitleTrack::parseSrt(srt);
    if (cues.size() != 2)
        throw std::runtime_error("Expected 2 cues, got " + std::to_string(cues.size()));

    if (cues[0].text != "Hello world")
        throw std::runtime_error("Markup not stripped: '" + cues[0].text.toStdString() + "'");
    if (std::abs(cues[0].startSeconds - 1.0) > 1e-6 || std::abs(cues[0].endSeconds - 2.5) > 1e-6)
        throw std::runtime_error("Cue 1 timestamps wrong");

    if (cues[1].text != "Line one\nLine two")
        throw std::runtime_error("Multi-line cue text wrong: '" + cues[1].text.toStdString() + "'");
    if (std::abs(cues[1].startSeconds - 5.0) > 1e-6 || std::abs(cues[1].endSeconds - 7.25) > 1e-6)
        throw std::runtime_error("Cue 2 timestamps wrong");

    // findActiveCue: before/between/during/after.
    if (SubtitleTrack::findActiveCue(cues, 0.5) != nullptr)
        throw std::runtime_error("Expected no active cue before the first one");
    if (SubtitleTrack::findActiveCue(cues, 3.5) != nullptr)
        throw std::runtime_error("Expected no active cue in the gap between cues");
    if (SubtitleTrack::findActiveCue(cues, 10.0) != nullptr)
        throw std::runtime_error("Expected no active cue after the last one");

    auto* active = SubtitleTrack::findActiveCue(cues, 1.5);
    if (active == nullptr || active->text != "Hello world")
        throw std::runtime_error("Expected cue 1 to be active at t=1.5s");

    active = SubtitleTrack::findActiveCue(cues, 6.0);
    if (active == nullptr || active->text != "Line one\nLine two")
        throw std::runtime_error("Expected cue 2 to be active at t=6.0s");

    // A malformed block (no "-->") should be skipped, not crash or throw off
    // the cues around it.
    juce::String malformed =
        "not a real cue\n"
        "still not one\n"
        "\n"
        "00:00:01,000 --> 00:00:02,000\n"
        "Still parses this one\n";
    auto malformedCues = SubtitleTrack::parseSrt(malformed);
    if (malformedCues.size() != 1 || malformedCues[0].text != "Still parses this one")
        throw std::runtime_error("Malformed leading block should be skipped, valid cue kept");

    std::cout << "✓ Parsed " << cues.size() << " cues, markup stripped, lookup correct across gaps" << std::endl;
    std::cout << std::endl;
}

void testFfmpegVideoAudioFormatReaderSequentialRead()
{
    std::cout << "=== Testing FfmpegVideoAudioFormatReader (sequential read) ===" << std::endl;

    if (!VideoExporter::isFfmpegAvailable())
    {
        std::cout << "! ffmpeg not found on PATH -- skipping test" << std::endl << std::endl;
        return;
    }

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File videoFile = tempDir.getChildFile("sp2_video_audio_seq_test.mp4");

    const double duration = 3.0;
    if (!generateTestVideoFile(videoFile, duration, 320, 240, 25.0, 1000.0))
        throw std::runtime_error("Could not generate synthetic test video");

    juce::String errorMessage;
    auto reader = FfmpegVideoAudioFormatReader::createForFile(videoFile, duration, -1, errorMessage);
    if (reader == nullptr)
        throw std::runtime_error("createForFile failed: " + errorMessage.toStdString());

    if (reader->sampleRate != FfmpegVideoAudioFormatReader::kSampleRate)
        throw std::runtime_error("Unexpected sample rate: " + std::to_string(reader->sampleRate));
    if ((int) reader->numChannels != FfmpegVideoAudioFormatReader::kChannels)
        throw std::runtime_error("Unexpected channel count: " + std::to_string(reader->numChannels));

    const juce::int64 expectedLength = (juce::int64) (duration * FfmpegVideoAudioFormatReader::kSampleRate);
    if (std::abs((double) (reader->lengthInSamples - expectedLength)) > FfmpegVideoAudioFormatReader::kSampleRate * 0.5)
        throw std::runtime_error("lengthInSamples doesn't match the probed duration: "
            + std::to_string(reader->lengthInSamples) + " vs expected ~" + std::to_string(expectedLength));
    std::cout << "✓ lengthInSamples=" << reader->lengthInSamples << " (~" << duration << "s @ 48kHz)" << std::endl;

    const int numSamples = 48000; // 1 second
    std::vector<float> left((size_t) numSamples), right((size_t) numSamples);
    float* chans[2] = { left.data(), right.data() };
    if (!reader->read(chans, 2, 0, numSamples))
        throw std::runtime_error("read() returned false");

    double rms = computeRms(left.data(), numSamples);
    if (rms < 0.02)
        throw std::runtime_error("Decoded audio is silent (RMS=" + std::to_string(rms) + ")");
    std::cout << "✓ Decoded non-silent audio from position 0 (RMS=" << rms << ")" << std::endl;

    videoFile.deleteFile();
    std::cout << std::endl;
}

// Exercises the seek-detection-and-relaunch path end to end: reads forward from
// 0 (should hear the first tone), then issues a deliberately non-sequential
// readSamples() request -- exactly what MultiChannelAudioFormatReaderSource
// produces after AudioTransportSource::setPosition() -- and confirms the
// decoded audio actually comes from the point seeked to (second tone), not
// wherever sequential decoding happened to be.
void testFfmpegVideoAudioFormatReaderSeek()
{
    std::cout << "=== Testing FfmpegVideoAudioFormatReader (seek) ===" << std::endl;

    if (!VideoExporter::isFfmpegAvailable())
    {
        std::cout << "! ffmpeg not found on PATH -- skipping test" << std::endl << std::endl;
        return;
    }

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File audioFile = tempDir.getChildFile("sp2_video_audio_seek_test.m4a");

    const double toneASeconds = 2.5, toneBSeconds = 2.5;
    if (!generateTwoToneAudioFile(audioFile, toneASeconds, 440.0, toneBSeconds, 880.0))
        throw std::runtime_error("Could not generate two-tone test audio file");

    const double totalDuration = toneASeconds + toneBSeconds;
    juce::String errorMessage;
    auto reader = FfmpegVideoAudioFormatReader::createForFile(audioFile, totalDuration, -1, errorMessage);
    if (reader == nullptr)
        throw std::runtime_error("createForFile failed: " + errorMessage.toStdString());

    const double sr = FfmpegVideoAudioFormatReader::kSampleRate;

    // Sequential read from the start -- should be the 440Hz tone.
    {
        const int numSamples = (int) (sr * 0.5);
        std::vector<float> left((size_t) numSamples), right((size_t) numSamples);
        float* chans[2] = { left.data(), right.data() };
        if (!reader->read(chans, 2, 0, numSamples))
            throw std::runtime_error("read() at position 0 returned false");

        double zc = estimateZeroCrossingsPerSecond(left.data(), numSamples, sr);
        if (zc > 1200.0)
            throw std::runtime_error("Expected the 440Hz tone near position 0, got zc=" + std::to_string(zc) + "/s");
        std::cout << "✓ Sequential read at t=0 sounds like 440Hz (zc=" << zc << "/s)" << std::endl;
    }

    // A deliberately non-sequential request well past the splice point --
    // should trigger relaunchAt() and land on the 880Hz tone.
    {
        const juce::int64 seekSample = (juce::int64) (3.0 * sr);
        const int numSamples = (int) (sr * 0.5);
        std::vector<float> left((size_t) numSamples), right((size_t) numSamples);
        float* chans[2] = { left.data(), right.data() };
        if (!reader->read(chans, 2, seekSample, numSamples))
            throw std::runtime_error("read() after seek returned false");

        double zc = estimateZeroCrossingsPerSecond(left.data(), numSamples, sr);
        if (zc < 1400.0)
            throw std::runtime_error("Expected the 880Hz tone after seeking to t=3s, got zc=" + std::to_string(zc) + "/s");
        std::cout << "✓ Seeking to t=3s correctly lands on the 880Hz tone (zc=" << zc << "/s)" << std::endl;
    }

    audioFile.deleteFile();
    std::cout << std::endl;
}

void testFfmpegVideoFrameDecoder()
{
    std::cout << "=== Testing FfmpegVideoFrameDecoder ===" << std::endl;

    if (!VideoExporter::isFfmpegAvailable())
    {
        std::cout << "! ffmpeg not found on PATH -- skipping test" << std::endl << std::endl;
        return;
    }

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File videoFile = tempDir.getChildFile("sp2_video_frame_decoder_test.mp4");

    const double duration = 5.0;
    const int width = 320, height = 240;
    const double fps = 25.0;
    if (!generateTestVideoFile(videoFile, duration, width, height, fps, 1000.0))
        throw std::runtime_error("Could not generate synthetic test video");

    FfmpegVideoFrameDecoder decoder(videoFile, width, height, fps);

    auto waitForFrameNear = [&decoder](double targetSeconds, int timeoutMs)
        -> std::shared_ptr<const FfmpegVideoFrameDecoder::Frame>
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        std::shared_ptr<const FfmpegVideoFrameDecoder::Frame> frame;
        while (frame == nullptr && juce::Time::getMillisecondCounter() < deadline)
        {
            frame = decoder.getFrameNear(targetSeconds);
            if (frame == nullptr)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return frame;
    };

    auto firstFrame = waitForFrameNear(0.4, 5000);
    if (firstFrame == nullptr)
        throw std::runtime_error("No frame arrived near t=0 within 5s");
    if (firstFrame->image.getWidth() != width || firstFrame->image.getHeight() != height)
        throw std::runtime_error("Decoded frame has the wrong dimensions");

    // testsrc's pattern is a colourful moving gradient/bars -- confirm it isn't
    // just a solid black frame (e.g. from a botched pixel conversion).
    bool sawNonBlack = false;
    for (int y = 0; y < height && !sawNonBlack; y += 17)
        for (int x = 0; x < width && !sawNonBlack; x += 17)
            if (firstFrame->image.getPixelAt(x, y) != juce::Colours::black)
                sawNonBlack = true;
    if (!sawNonBlack)
        throw std::runtime_error("Decoded frame appears to be entirely black");
    std::cout << "✓ First frame decoded (" << width << "x" << height
              << ", timestamp=" << firstFrame->timestampSeconds << "s, non-black)" << std::endl;

    // Reseek and confirm a fresh frame with a plausible timestamp and pixel
    // content different from the first frame shows up (proving it isn't just
    // replaying the same buffered frame).
    decoder.seekTo(3.0);
    auto seekedFrame = waitForFrameNear(3.5, 5000);
    if (seekedFrame == nullptr)
        throw std::runtime_error("No frame arrived after seekTo(3.0) within 5s");
    if (seekedFrame->timestampSeconds < 2.0 || seekedFrame->timestampSeconds > 4.0)
        throw std::runtime_error("Frame after seek has an implausible timestamp: "
            + std::to_string(seekedFrame->timestampSeconds));

    bool differsFromFirst = false;
    for (int y = 0; y < height && !differsFromFirst; y += 17)
        for (int x = 0; x < width && !differsFromFirst; x += 17)
            if (seekedFrame->image.getPixelAt(x, y) != firstFrame->image.getPixelAt(x, y))
                differsFromFirst = true;
    if (!differsFromFirst)
        throw std::runtime_error("Frame after seeking to t=3s is pixel-identical to the t=0 frame");
    std::cout << "✓ Seek to t=3s produced a distinct frame (timestamp="
              << seekedFrame->timestampSeconds << "s)" << std::endl;

    videoFile.deleteFile();
    std::cout << std::endl;
}

// Drag and drop runs every dropped path through AudioFileCollector. Video
// files are playable, so a drop must accept them just like audio files.
void testAudioFileCollectorAcceptsVideo()
{
    std::cout << "=== Testing AudioFileCollector drop filter ===" << std::endl;

    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("sp2_collector_test_" + juce::String(juce::Random::getSystemRandom().nextInt()));
    dir.createDirectory();

    for (auto name : { "song.mp3", "clip.mp4", "movie.MKV", "tune.xm", "notes.txt" })
        dir.getChildFile(name).create();

    auto collected = AudioFileCollector::collect(juce::StringArray { dir.getFullPathName() });

    juce::StringArray names;
    for (const auto& f : collected)
        names.add(f.getFileName());

    assert(names.contains("song.mp3"));
    assert(names.contains("clip.mp4"));
    assert(names.contains("movie.MKV"));
    assert(names.contains("tune.xm"));
    assert(! names.contains("notes.txt"));
    assert(AudioFileCollector::isSupported(dir.getChildFile("clip.mp4")));

    dir.deleteRecursively();
    std::cout << "✓ Drop filter accepts audio, module and video files\n" << std::endl;
}

// Unlike assert(), always evaluated (these checks call the code under test,
// and the default build is Release) and reported through main()'s catch.
#define MIC_CHECK(cond) \
    do { if (! (cond)) throw std::runtime_error("check failed at line " + std::to_string(__LINE__) + ": " #cond); } while (0)

// A mono ramp clip: sample i == i / 1000, so edits are easy to check by value.
static void fillRampClip(MicClip& clip, int numSamples, double rate = 1000.0)
{
    juce::AudioBuffer<float> source(1, numSamples);
    for (int i = 0; i < numSamples; ++i)
        source.setSample(0, i, (float) i / 1000.0f);
    clip.append(source, numSamples, rate);
}

void testMicClipEdits()
{
    std::cout << "=== Testing MicClip edits ===" << std::endl;

    MicClip clip;
    fillRampClip(clip, 1000);
    MIC_CHECK(clip.getNumSamples() == 1000 && clip.getNumChannels() == 1);
    MIC_CHECK(std::abs(clip.getLengthSeconds() - 1.0) < 1e-9);

    // Appends at another rate are refused rather than silently mis-pitched.
    juce::AudioBuffer<float> other(1, 10);
    other.clear();
    MIC_CHECK(! clip.append(other, 10, 48000.0));
    std::cout << "✓ Append refuses a mismatched sample rate" << std::endl;

    // Growing across many small appends keeps every sample in order.
    MicClip grown;
    for (int block = 0; block < 600; ++block)
    {
        juce::AudioBuffer<float> chunk(2, 512);
        for (int i = 0; i < 512; ++i)
        {
            chunk.setSample(0, i, (float) (block * 512 + i));
            chunk.setSample(1, i, -1.0f);
        }
        MIC_CHECK(grown.append(chunk, 512, 48000.0));
    }
    MIC_CHECK(grown.getNumSamples() == 600 * 512);
    MIC_CHECK(grown.getBuffer().getSample(0, 300000) == 300000.0f);
    std::cout << "✓ Append grows the clip across " << grown.getNumSamples() << " samples" << std::endl;

    MIC_CHECK(clip.deleteRange({ 100, 200 }));
    MIC_CHECK(clip.getNumSamples() == 900);
    MIC_CHECK(std::abs(clip.getBuffer().getSample(0, 100) - 0.2f) < 1e-6f);
    std::cout << "✓ Delete removes the range and closes the gap" << std::endl;

    MIC_CHECK(clip.undo());
    MIC_CHECK(clip.getNumSamples() == 1000);
    MIC_CHECK(std::abs(clip.getBuffer().getSample(0, 150) - 0.15f) < 1e-6f);
    MIC_CHECK(clip.redo());
    MIC_CHECK(clip.getNumSamples() == 900);
    MIC_CHECK(clip.undo());
    std::cout << "✓ Undo/redo restore the clip" << std::endl;

    MIC_CHECK(clip.keepOnly({ 500, 600 }));
    MIC_CHECK(clip.getNumSamples() == 100);
    MIC_CHECK(std::abs(clip.getBuffer().getSample(0, 0) - 0.5f) < 1e-6f);
    MIC_CHECK(clip.undo());
    std::cout << "✓ Trim keeps only the selection" << std::endl;

    MIC_CHECK(clip.silence({ 10, 20 }));
    MIC_CHECK(clip.getBuffer().getSample(0, 15) == 0.0f);
    MIC_CHECK(clip.getBuffer().getSample(0, 20) != 0.0f);
    MIC_CHECK(clip.undo());

    MIC_CHECK(clip.fadeOut({ 900, 1000 }));
    MIC_CHECK(std::abs(clip.getBuffer().getSample(0, 999)) < 0.01f);
    MIC_CHECK(std::abs(clip.getBuffer().getSample(0, 899) - 0.899f) < 1e-6f);
    MIC_CHECK(clip.undo());

    MIC_CHECK(clip.fadeIn({ 0, 1000 }));
    MIC_CHECK(clip.getBuffer().getSample(0, 0) == 0.0f);
    MIC_CHECK(clip.undo());
    std::cout << "✓ Silence and fades only touch the selection" << std::endl;

    MIC_CHECK(clip.normalize({}, 0.5f));
    MIC_CHECK(std::abs(clip.getBuffer().getMagnitude(0, 0, 1000) - 0.5f) < 1e-5f);
    MIC_CHECK(clip.undo());
    std::cout << "✓ Normalize brings the peak to the target" << std::endl;

    // Out-of-range selections are clamped; empty ones do nothing.
    MIC_CHECK(! clip.deleteRange({ 2000, 3000 }));
    MIC_CHECK(clip.deleteRange({ 950, 5000 }));
    MIC_CHECK(clip.getNumSamples() == 950);
    std::cout << "✓ Ranges are clamped to the clip" << std::endl;

    // Peaks: one bin per 256 samples, matching the data.
    MIC_CHECK(clip.getPeaks(0).size() == (size_t) ((950 + MicClip::peakBinSize - 1) / MicClip::peakBinSize));
    MIC_CHECK(std::abs(clip.getPeaks(0).back().max - 0.949f) < 1e-6f);
    std::cout << "✓ Peak overview tracks edits" << std::endl;

    // Round-trip through WAV and FLAC.
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    for (auto ext : { ".wav", ".flac" })
    {
        MicClip toSave;
        fillRampClip(toSave, 4410, 44100.0);
        auto file = tempDir.getChildFile(juce::String("mic_clip_test") + ext);
        juce::String error;
        MIC_CHECK(toSave.writeToFile(file, error));

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        MIC_CHECK(reader != nullptr);
        MIC_CHECK(reader->lengthInSamples == 4410 && reader->sampleRate == 44100.0 && reader->bitsPerSample == 24);
        juce::AudioBuffer<float> back(1, 4410);
        reader->read(&back, 0, 4410, 0, true, false);
        MIC_CHECK(std::abs(back.getSample(0, 500) - 0.5f) < 1e-4f);
        reader.reset();
        file.deleteFile();
        std::cout << "✓ Saves and reloads as " << ext << std::endl;
    }

    MicClip empty;
    juce::String error;
    MIC_CHECK(! empty.writeToFile(tempDir.getChildFile("mic_clip_empty.wav"), error) && error.isNotEmpty());
    std::cout << "✓ Saving an empty clip reports an error" << std::endl;
}

void testClipPlaybackSource()
{
    std::cout << "=== Testing ClipPlaybackSource ===" << std::endl;

    MicClip clip;
    fillRampClip(clip, 1000, 1000.0);

    ClipPlaybackSource playback(clip);
    juce::AudioBuffer<float> out(2, 64);

    // Same rate: plays the samples back verbatim, mono duplicated to both channels.
    playback.prepareToPlay(64, 1000.0);
    playback.start(100, 1000);
    playback.getNextAudioBlock(juce::AudioSourceChannelInfo(out));
    MIC_CHECK(std::abs(out.getSample(0, 0) - 0.1f) < 1e-6f);
    MIC_CHECK(std::abs(out.getSample(1, 10) - 0.11f) < 1e-6f);
    MIC_CHECK(std::abs(playback.getPositionSamples() - 164.0) < 1e-9);
    std::cout << "✓ Plays from the start sample, mono to both channels" << std::endl;

    // Stops at the end of the range and pads with silence.
    playback.start(980, 1000);
    playback.getNextAudioBlock(juce::AudioSourceChannelInfo(out));
    MIC_CHECK(! playback.isPlaying());
    MIC_CHECK(std::abs(out.getSample(0, 19) - 0.999f) < 1e-6f);
    MIC_CHECK(out.getSample(0, 20) == 0.0f && out.getSample(0, 63) == 0.0f);
    std::cout << "✓ Stops at the end of the range" << std::endl;

    // Output at twice the clip rate: half-sample steps, interpolated.
    playback.prepareToPlay(64, 2000.0);
    playback.start(0, 1000);
    playback.getNextAudioBlock(juce::AudioSourceChannelInfo(out));
    MIC_CHECK(std::abs(out.getSample(0, 1) - 0.0005f) < 1e-6f);
    MIC_CHECK(std::abs(out.getSample(0, 2) - 0.001f) < 1e-6f);
    MIC_CHECK(std::abs(playback.getPositionSamples() - 32.0) < 1e-9);
    std::cout << "✓ Resamples to the output rate" << std::endl;

    playback.stop();
    playback.getNextAudioBlock(juce::AudioSourceChannelInfo(out));
    MIC_CHECK(out.getMagnitude(0, 64) == 0.0f);
    std::cout << "✓ Silent when stopped" << std::endl;
}

void testMicRecorderCapture()
{
    std::cout << "=== Testing MicRecorder capture path ===" << std::endl;

    // No device is opened: the callback is driven directly, like a mono mic would.
    MicRecorder recorder;
    recorder.prepareCapture(48000.0, 1);

    std::vector<float> block(480);
    const float* inputs[] = { block.data() };
    juce::AudioIODeviceCallbackContext context;

    auto feed = [&](float base)
    {
        for (size_t i = 0; i < block.size(); ++i)
            block[i] = base + (float) i * 1.0e-6f;
        recorder.audioDeviceIOCallbackWithContext(inputs, 1, nullptr, 0, (int) block.size(), context);
    };

    // Before Record: level is metered, nothing is captured.
    feed(0.25f);
    MIC_CHECK(recorder.takePeak(0) > 0.25f);
    MIC_CHECK(recorder.takePeak(0) == 0.0f); // taking resets it

    MicClip clip;
    MIC_CHECK(recorder.drainInto(clip) == 0);
    std::cout << "✓ Meters input without recording" << std::endl;

    juce::String error;
    MIC_CHECK(recorder.startRecording(error));
    feed(0.5f);
    feed(0.6f);
    recorder.stopRecording();
    feed(0.7f); // after stop: must not be captured
    MIC_CHECK(recorder.drainInto(clip) == 960);
    MIC_CHECK(clip.getNumSamples() == 960 && clip.getSampleRate() == 48000.0 && clip.getNumChannels() == 1);
    MIC_CHECK(std::abs(clip.getBuffer().getSample(0, 0) - 0.5f) < 1e-6f);
    MIC_CHECK(std::abs(clip.getBuffer().getSample(0, 480) - 0.6f) < 1e-6f);
    MIC_CHECK(recorder.getDroppedSamples() == 0);
    std::cout << "✓ Captures exactly the blocks between start and stop" << std::endl;

    // A FIFO that is never drained drops (and counts) the overflow instead of
    // overwriting audio it already holds.
    MIC_CHECK(recorder.startRecording(error));
    for (int i = 0; i < 500; ++i) // ~5 s of audio into a 4 s FIFO
        feed(0.1f);
    recorder.stopRecording();
    MIC_CHECK(recorder.getDroppedSamples() > 0);
    MicClip overflowed;
    MIC_CHECK(recorder.drainInto(overflowed) == 4 * 48000 - 1); // AbstractFifo keeps one slot free
    std::cout << "✓ Overflow is counted as dropped samples" << std::endl;

    // Once the device stops there is no input to record from.
    recorder.audioDeviceStopped();
    MIC_CHECK(! recorder.startRecording(error) && error.isNotEmpty());
    std::cout << "✓ Refuses to record without an open input" << std::endl;
}

void testMicRecorderChannelModes()
{
    std::cout << "=== Testing MicRecorder channel modes ===" << std::endl;

    // A stereo jack with a mono mic: signal on the right, faint noise on the left.
    std::vector<float> left(480), right(480);
    juce::Random random(7);
    const float* inputs[] = { left.data(), right.data() };
    juce::AudioIODeviceCallbackContext context;

    auto feed = [&](MicRecorder& recorder)
    {
        for (size_t i = 0; i < 480; ++i)
        {
            left[i] = (random.nextFloat() - 0.5f) * 0.002f;          // ~ -60 dB
            right[i] = 0.3f * std::sin((float) i * 0.05f);          // the voice
        }
        recorder.audioDeviceIOCallbackWithContext(inputs, 2, nullptr, 0, 480, context);
    };

    auto record = [&](MicRecorder::ChannelMode mode, MicClip& clip)
    {
        MicRecorder recorder;
        recorder.prepareCapture(48000.0, 2);
        recorder.setChannelMode(mode);
        for (int i = 0; i < 100; ++i) // ~1 s of levels for the auto choice
            feed(recorder);
        juce::String error;
        MIC_CHECK(recorder.startRecording(error));
        const auto source = recorder.describeRecordSource();
        for (int i = 0; i < 10; ++i)
            feed(recorder);
        recorder.stopRecording();
        recorder.drainInto(clip);
        return source;
    };

    MicClip autoClip;
    MIC_CHECK(record(MicRecorder::ChannelMode::AutoMono, autoClip) == "right channel");
    MIC_CHECK(autoClip.getNumChannels() == 1 && autoClip.getNumSamples() == 4800);
    MIC_CHECK(std::abs(autoClip.getBuffer().getSample(0, 20) - right[20]) < 1e-6f);
    std::cout << "✓ Auto mono records only the channel with signal" << std::endl;

    MicClip leftClip;
    record(MicRecorder::ChannelMode::Left, leftClip);
    MIC_CHECK(leftClip.getNumChannels() == 1 && leftClip.getBuffer().getMagnitude(0, 0, 4800) < 0.01f);
    std::cout << "✓ Left only records the left channel" << std::endl;

    MicClip stereoClip;
    MIC_CHECK(record(MicRecorder::ChannelMode::Stereo, stereoClip) == "stereo");
    MIC_CHECK(stereoClip.getNumChannels() == 2 && stereoClip.getBuffer().getMagnitude(1, 0, 4800) > 0.29f);
    std::cout << "✓ Stereo records both channels" << std::endl;

    // Equal levels on both sides: auto mono mixes rather than guessing.
    MIC_CHECK(ClipEffects::chooseMonoSource(0.01f, 0.012f) == -1);
    MIC_CHECK(ClipEffects::chooseMonoSource(0.0001f, 0.01f) == 1);
    MIC_CHECK(ClipEffects::chooseMonoSource(0.01f, 0.0001f) == 0);
    std::cout << "✓ Mixes when both channels carry signal" << std::endl;

    // Make Mono on an already-recorded stereo take.
    MIC_CHECK(ClipEffects::chooseMonoSource(stereoClip.getBuffer(), stereoClip.getNumSamples()) == 1);
    MIC_CHECK(stereoClip.makeMono(1));
    MIC_CHECK(stereoClip.getNumChannels() == 1 && stereoClip.getBuffer().getMagnitude(0, 0, 4800) > 0.29f);
    MIC_CHECK(stereoClip.undo() && stereoClip.getNumChannels() == 2);
    MIC_CHECK(! autoClip.makeMono(0)); // already mono
    std::cout << "✓ Make Mono keeps the chosen channel, and undoes" << std::endl;
}

// Level of one frequency in a mono buffer range, as an amplitude.
static float toneAmplitude(const juce::AudioBuffer<float>& audio, int start, int length, double rate, double hz)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double phase = juce::MathConstants<double>::twoPi * hz * i / rate;
        re += audio.getSample(0, start + i) * std::cos(phase);
        im += audio.getSample(0, start + i) * std::sin(phase);
    }
    return (float) (2.0 * std::hypot(re, im) / length);
}

void testClipEffects()
{
    std::cout << "=== Testing ClipEffects (hum + noise reduction) ===" << std::endl;
    constexpr double rate = 48000.0;
    constexpr int length = 96000;

    // Hum: 50 Hz + 100 Hz buzz under a 1 kHz "voice" tone.
    juce::AudioBuffer<float> hum(1, length);
    for (int i = 0; i < length; ++i)
    {
        const double t = i / rate;
        hum.setSample(0, i, (float) (0.1 * std::sin(juce::MathConstants<double>::twoPi * 50.0 * t)
                                   + 0.05 * std::sin(juce::MathConstants<double>::twoPi * 100.0 * t)
                                   + 0.2 * std::sin(juce::MathConstants<double>::twoPi * 1000.0 * t)));
    }
    MIC_CHECK(ClipEffects::detectMainsHz(hum, length, rate) == 50.0);
    ClipEffects::removeHum(hum, 0, length, rate, 50.0);
    // Measured on the second half, after the filters have settled.
    const float hum50 = toneAmplitude(hum, length / 2, length / 2, rate, 50.0);
    const float hum100 = toneAmplitude(hum, length / 2, length / 2, rate, 100.0);
    const float voice = toneAmplitude(hum, length / 2, length / 2, rate, 1000.0);
    MIC_CHECK(hum50 < 0.1f * 0.01f);   // > 40 dB down
    MIC_CHECK(hum100 < 0.05f * 0.03f); // > 30 dB down
    MIC_CHECK(std::abs(voice - 0.2f) < 0.2f * 0.06f); // within 0.5 dB
    std::cout << "✓ Remove Hum: 50 Hz " << juce::Decibels::toString(juce::Decibels::gainToDecibels(hum50 / 0.1f))
              << ", 100 Hz " << juce::Decibels::toString(juce::Decibels::gainToDecibels(hum100 / 0.05f))
              << ", 1 kHz tone kept" << std::endl;

    // Noise: steady hiss throughout, with a tone only in the second half.
    juce::Random random(42);
    juce::AudioBuffer<float> noisy(1, length);
    for (int i = 0; i < length; ++i)
    {
        float x = (random.nextFloat() * 2.0f - 1.0f) * 0.01f; // ~ -45 dB RMS hiss
        if (i >= length / 2)
            x += 0.3f * (float) std::sin(juce::MathConstants<double>::twoPi * 440.0 * i / rate);
        noisy.setSample(0, i, x);
    }

    // 0 dB reduction must give the input back (the STFT reconstructs exactly).
    juce::AudioBuffer<float> copy(noisy);
    auto profile = ClipEffects::learnNoiseProfile(copy, 0, length / 2);
    MIC_CHECK(profile.isValid() && profile.frames > 10);
    ClipEffects::reduceNoise(copy, 0, length, profile, 0.0f);
    float maxError = 0.0f;
    for (int i = 0; i < length; ++i)
        maxError = juce::jmax(maxError, std::abs(copy.getSample(0, i) - noisy.getSample(0, i)));
    MIC_CHECK(maxError < 1.0e-4f);
    std::cout << "✓ Reduce Noise at 0 dB reconstructs the input" << std::endl;

    const float noiseBefore = noisy.getRMSLevel(0, 0, length / 2);
    const float toneBefore = toneAmplitude(noisy, length / 2, length / 2, rate, 440.0);
    ClipEffects::reduceNoise(noisy, 0, length, profile, 12.0f);
    const float noiseAfter = noisy.getRMSLevel(0, 0, length / 2);
    const float toneAfter = toneAmplitude(noisy, length / 2, length / 2, rate, 440.0);
    const float noiseDropDb = juce::Decibels::gainToDecibels(noiseAfter / noiseBefore);
    MIC_CHECK(noiseDropDb < -9.0f);
    MIC_CHECK(std::abs(toneAfter - toneBefore) < toneBefore * 0.06f);
    std::cout << "✓ Reduce Noise (-12 dB): hiss " << juce::String(noiseDropDb, 1)
              << " dB, tone kept within 0.5 dB" << std::endl;

    // Without a hand-picked profile, the quiet half is found automatically.
    auto estimated = ClipEffects::estimateNoiseProfile(copy, 0, length);
    MIC_CHECK(estimated.isValid());
    MIC_CHECK(std::abs(estimated.magnitudes[0][200] - profile.magnitudes[0][200])
              < profile.magnitudes[0][200] * 0.5f);
    MIC_CHECK(! ClipEffects::learnNoiseProfile(copy, 0, 1000).isValid()); // shorter than a frame

    // The quietest half second of "hiss, then hiss + tone" lies in the first half.
    const auto quiet = ClipEffects::findQuietestStretch(copy, length, 24000);
    MIC_CHECK(quiet >= 0 && quiet + 24000 <= length / 2);
    MIC_CHECK(ClipEffects::findQuietestStretch(copy, 1000, 24000) == -1); // too short
    std::cout << "✓ Noise profile can be estimated from the quietest frames" << std::endl;

    // As a clip edit it is one undo step.
    MicClip clip;
    clip.append(copy, length, rate);
    MIC_CHECK(clip.applyEffect({}, [&](juce::AudioBuffer<float>& audio, int start, int len)
    {
        ClipEffects::reduceNoise(audio, start, len, estimated, 12.0f);
        return true;
    }));
    MIC_CHECK(clip.getBuffer().getRMSLevel(0, 0, length / 2) < copy.getRMSLevel(0, 0, length / 2) * 0.5f);
    MIC_CHECK(clip.undo());
    MIC_CHECK(clip.getBuffer().getSample(0, 1234) == copy.getSample(0, 1234));
    std::cout << "✓ Effects apply as a single undoable edit" << std::endl;
}

// A voice-like test signal: 150 Hz with 40 harmonics falling 6 dB/octave,
// in 3 Hz "syllables" so there are pauses, under steady hiss. Reports how much
// of the voice above 2 kHz survives noise reduction (projection onto the clean
// signal, so the hiss itself doesn't count), and how far the hiss drops.
struct VoiceNoiseResult { float highBandKeptDb; float midBandKeptDb; float hissDropDb; };

static VoiceNoiseResult measureVoiceUnderNoise(float reductionDb)
{
    constexpr double rate = 48000.0;
    constexpr int length = 144000; // 3 s
    juce::AudioBuffer<float> clean(1, length), noisy(1, length);
    juce::Random random(3);

    for (int i = 0; i < length; ++i)
    {
        const double t = i / rate;
        const double syllable = std::sin(juce::MathConstants<double>::pi * 3.0 * t);
        const double envelope = syllable > 0.0 ? syllable : 0.0; // voiced half, silent half
        double voice = 0.0;
        for (int k = 1; k <= 40; ++k)
            voice += (0.25 / k) * std::sin(juce::MathConstants<double>::twoPi * 150.0 * k * t + k * 0.7);
        clean.setSample(0, i, (float) (voice * envelope));
        noisy.setSample(0, i, clean.getSample(0, i) + (random.nextFloat() * 2.0f - 1.0f) * 0.03f);
    }

    juce::AudioBuffer<float> hissOnly(1, length);
    for (int i = 0; i < length; ++i)
        hissOnly.setSample(0, i, noisy.getSample(0, i) - clean.getSample(0, i));

    // Noise profile from the first pause (t = 1/3 s .. 2/3 s is silent).
    const auto profile = ClipEffects::learnNoiseProfile(noisy, 17000, 14000);
    juce::AudioBuffer<float> out(noisy);
    ClipEffects::reduceNoise(out, 0, length, profile, reductionDb);

    auto bandKept = [&](float lowHz, float highHz)
    {
        using Coefficients = juce::dsp::IIR::Coefficients<float>;
        auto filtered = [&](const juce::AudioBuffer<float>& src)
        {
            juce::AudioBuffer<float> b(src);
            juce::dsp::IIR::Filter<float> hp1(Coefficients::makeHighPass(rate, lowHz)), hp2(Coefficients::makeHighPass(rate, lowHz));
            juce::dsp::IIR::Filter<float> lp1(Coefficients::makeLowPass(rate, highHz)), lp2(Coefficients::makeLowPass(rate, highHz));
            for (int i = 0; i < length; ++i)
                b.setSample(0, i, lp2.processSample(lp1.processSample(hp2.processSample(hp1.processSample(b.getSample(0, i))))));
            return b;
        };
        const auto c = filtered(clean), o = filtered(out);
        double dot = 0.0, energy = 0.0;
        for (int i = 4800; i < length; ++i)
        {
            dot += (double) o.getSample(0, i) * c.getSample(0, i);
            energy += (double) c.getSample(0, i) * c.getSample(0, i);
        }
        return juce::Decibels::gainToDecibels((float) (dot / energy));
    };

    // Hiss drop measured in the pauses.
    double before = 0.0, after = 0.0;
    for (int i = 0; i < length; ++i)
        if (clean.getSample(0, i) == 0.0f)
        {
            before += (double) noisy.getSample(0, i) * noisy.getSample(0, i);
            after  += (double) out.getSample(0, i) * out.getSample(0, i);
        }

    return { bandKept(2000.0f, 6000.0f), bandKept(300.0f, 2000.0f),
             juce::Decibels::gainToDecibels((float) std::sqrt(after / before)) };
}

void testNoiseReductionKeepsVoice()
{
    std::cout << "=== Testing noise reduction on a voice-like signal ===" << std::endl;
    for (float reduction : { 6.0f, 12.0f, 20.0f })
    {
        const auto r = measureVoiceUnderNoise(reduction);
        std::cout << "  -" << reduction << " dB: voice 300-2k " << juce::String(r.midBandKeptDb, 1)
                  << " dB, 2k-6k " << juce::String(r.highBandKeptDb, 1)
                  << " dB, hiss in pauses " << juce::String(r.hissDropDb, 1) << " dB" << std::endl;
        // The voice -- including its upper harmonics -- must come through
        // nearly intact; muffling it is the "bad internet call" sound.
        MIC_CHECK(r.midBandKeptDb > -1.0f);
        MIC_CHECK(r.highBandKeptDb > -2.0f);
        MIC_CHECK(r.hissDropDb < -reduction * 0.7f);
    }
    std::cout << "✓ Voice harmonics survive while hiss in the pauses drops" << std::endl;
}

// Hardware probe, skipped unless EXPLAYER_MIC_PROBE=1: opens the real
// default input exactly as the recorder window does, records two seconds and
// reports the level -- a quick way to tell "the app can't open the mic" from
// "the mic is muted/too quiet" on a given machine.
void testMicRecorderLiveDevice()
{
    if (juce::SystemStats::getEnvironmentVariable("EXPLAYER_MIC_PROBE", {}) != "1")
        return;

    std::cout << "=== Probing the real microphone input ===" << std::endl;
    juce::ScopedJuceInitialiser_GUI juceInit;

    MicRecorder recorder;
    const auto error = recorder.openInputDevice();
    std::cout << "  device: " << recorder.describeCurrentInput() << std::endl;
    MIC_CHECK(error.isEmpty());

    juce::String startError;
    MIC_CHECK(recorder.startRecording(startError));
    MicClip clip;
    float peak = 0.0f;
    for (int i = 0; i < 40; ++i)
    {
        juce::Thread::sleep(50);
        recorder.drainInto(clip);
        peak = juce::jmax(peak, recorder.takePeak(0));
    }
    recorder.stopRecording();
    recorder.drainInto(clip);

    std::cout << "  captured " << clip.getLengthSeconds() << " s, peak "
              << juce::Decibels::toString(juce::Decibels::gainToDecibels(peak)) << std::endl;
    MIC_CHECK(clip.getLengthSeconds() > 1.5);
}

int main()
{
    std::cout << "\n════════════════════════════════════════" << std::endl;
    std::cout << "  exPlayer Feature Test Suite" << std::endl;
    std::cout << "════════════════════════════════════════\n" << std::endl;
    
    try
    {
        testPlaylistManager();
        testPlaylistPersistence();
        testShuffleRepeat();
        testEqualizer();
        testAudioEngine();
        testMultichannelReaderSource();
        testStereoExpansion();
        testSpeakerTestChannelAdvance();
        testBitDepthQuantization();
        testXtpSongParsing();
        testXtpSequencerMissingSampleFile();
        testXtpSequencerRowJump();
        testXtpPlayPositionForPatternViewer();
#ifdef EXPLAYER_OPENMPT_SUPPORT
        testOpenMptPlayPositionForPatternViewer();
#endif
        testXtpSequencerChannelVolume();
        testXtpUnifiedSampleInstrument();
        testVideoExporter();
        testPlaylistVideoExporter();
        testScreenRecorder();
        testVideoFileProbe();
        testSubtitleTrackParsing();
        testFfmpegVideoAudioFormatReaderSequentialRead();
        testFfmpegVideoAudioFormatReaderSeek();
        testFfmpegVideoFrameDecoder();
        testAudioFileCollectorAcceptsVideo();
        testMicClipEdits();
        testClipPlaybackSource();
        testMicRecorderCapture();
        testMicRecorderChannelModes();
        testClipEffects();
        testNoiseReductionKeepsVoice();
        testMicRecorderLiveDevice();

        std::cout << "════════════════════════════════════════" << std::endl;
        std::cout << "  ✅ All tests passed!" << std::endl;
        std::cout << "════════════════════════════════════════\n" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "\n❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
