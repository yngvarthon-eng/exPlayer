#include "FullScreenTransportBar.h"

#include <cmath>

namespace
{
    const juce::Colour barBg      { 0xff0c0c18 };
    const juce::Colour separator  { 0xff252540 };
    const juce::Colour iconNormal { 0xffb8c4d4 };
    const juce::Colour iconOver   { 0xffffffff };
    const juce::Colour iconDown   { 0xffff7a1a }; // the grid's play colour
    const juce::Colour textColour { 0xff8899aa };

    // All icons are drawn in a 0..10 box; ShapeButton scales them to fit.
    juce::Path triangle(float x0, float x1, bool pointRight)
    {
        juce::Path p;
        if (pointRight) p.addTriangle(x0, 1.0f, x1, 5.0f, x0, 9.0f);
        else            p.addTriangle(x1, 1.0f, x0, 5.0f, x1, 9.0f);
        return p;
    }

    // An invisible full-size frame so every icon scales to the same size,
    // whatever its own extent.
    juce::Path framed(juce::Path p)
    {
        p.startNewSubPath(0.0f, 0.0f);
        p.startNewSubPath(10.0f, 10.0f);
        return p;
    }

    juce::Path skipTrackIcon(bool forward)
    {
        juce::Path p = triangle(forward ? 1.0f : 3.0f, forward ? 7.0f : 9.0f, forward);
        p.addRectangle(forward ? 7.5f : 1.0f, 1.0f, 1.5f, 8.0f);
        return framed(p);
    }

    juce::Path seekIcon(bool forward)
    {
        juce::Path p = triangle(forward ? 0.5f : 5.0f, forward ? 5.0f : 9.5f, forward);
        p.addPath(triangle(forward ? 5.0f : 0.5f, forward ? 9.5f : 5.0f, forward));
        return framed(p);
    }

    juce::Path playIcon()  { return framed(triangle(2.0f, 9.0f, true)); }

    juce::Path pauseIcon()
    {
        juce::Path p;
        p.addRectangle(2.0f, 1.0f, 2.2f, 8.0f);
        p.addRectangle(5.8f, 1.0f, 2.2f, 8.0f);
        return framed(p);
    }

    juce::Path stopIcon()
    {
        juce::Path p;
        p.addRectangle(1.5f, 1.5f, 7.0f, 7.0f);
        return framed(p);
    }

    juce::String formatTime(double seconds)
    {
        if (seconds < 0.0 || ! std::isfinite(seconds)) seconds = 0.0;
        const int total = (int) seconds;
        const int h = total / 3600, m = (total / 60) % 60, s = total % 60;
        return h > 0 ? juce::String::formatted("%d:%02d:%02d", h, m, s)
                     : juce::String::formatted("%d:%02d", m, s);
    }
}

FullScreenTransportBar::FullScreenTransportBar(AudioEngine& engine, PlaylistManager& playlist)
    : audioEngine(engine), playlistManager(playlist)
{
    auto makeButton = [this](const juce::String& name, const juce::Path& shape, const juce::String& tip,
                             std::function<void()> action)
    {
        auto b = std::make_unique<juce::ShapeButton>(name, iconNormal, iconOver, iconDown);
        b->setShape(shape, false, true, false);
        b->setTooltip(tip);
        b->setWantsKeyboardFocus(false); // keys stay with the fullscreen window
        b->onClick = std::move(action);
        addAndMakeVisible(*b);
        return b;
    };

    prevTrackButton = makeButton("prev", skipTrackIcon(false), "Previous track (Left)",
                                 [this] { playlistManager.previousTrack(); });
    backButton      = makeButton("back", seekIcon(false), "Back 10 seconds",
                                 [this] { skipBy(-skipSeconds); });
    playPauseButton = makeButton("play", playIcon(), "Play / pause (Space)", [this]
    {
        if (audioEngine.isPlaying()) audioEngine.pause();
        else                         audioEngine.play();
        updatePlayPauseShape();
    });
    stopButton      = makeButton("stop", stopIcon(), "Stop (Backspace)", [this] { audioEngine.stop(); });
    forwardButton   = makeButton("fwd", seekIcon(true), "Forward 10 seconds",
                                 [this] { skipBy(skipSeconds); });
    nextTrackButton = makeButton("next", skipTrackIcon(true), "Next track (Right)",
                                 [this] { playlistManager.nextTrack(); });

    auto makePatternButton = [this](const juce::String& text, const juce::String& tip, int direction)
    {
        auto b = std::make_unique<juce::TextButton>(text, tip);
        b->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour(juce::TextButton::textColourOffId, iconNormal);
        b->setColour(juce::ComboBox::outlineColourId, separator);
        b->setWantsKeyboardFocus(false);
        b->onClick = [this, direction] { skipPattern(direction); };
        addAndMakeVisible(*b);
        return b;
    };
    prevPatternButton = makePatternButton("< Pat", "Previous pattern in the song order (Page Up)", -1);
    nextPatternButton = makePatternButton("Pat >", "Next pattern in the song order (Page Down)", 1);

    positionSlider.setRange(0.0, 1.0);
    positionSlider.setMouseClickGrabsKeyboardFocus(false);
    positionSlider.setWantsKeyboardFocus(false);
    positionSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    positionSlider.setColour(juce::Slider::trackColourId, iconDown.withAlpha(0.8f));
    positionSlider.setColour(juce::Slider::backgroundColourId, separator);
    positionSlider.onDragStart = [this] { draggingSlider = true; };
    positionSlider.onDragEnd   = [this]
    {
        draggingSlider = false;
        const double duration = audioEngine.getTotalDuration();
        if (duration > 0.0)
            audioEngine.setCurrentPosition(positionSlider.getValue() * duration);
    };
    addAndMakeVisible(positionSlider);

    for (auto* l : { &elapsedLabel, &totalLabel, &titleLabel })
    {
        l->setColour(juce::Label::textColourId, textColour);
        l->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
    }
    elapsedLabel.setJustificationType(juce::Justification::centredRight);
    titleLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff99bbdd));
    titleLabel.setMinimumHorizontalScale(0.7f);

    timerCallback();
    startTimerHz(10);
}

FullScreenTransportBar::~FullScreenTransportBar()
{
    stopTimer();
}

void FullScreenTransportBar::skipBy(double seconds)
{
    const double duration = audioEngine.getTotalDuration();
    if (duration <= 0.0) return; // streams: nothing to seek in
    const double target = juce::jlimit(0.0, juce::jmax(0.0, duration - 0.5),
                                       audioEngine.getCurrentPlaybackTime() + seconds);
    audioEngine.setCurrentPosition(target);
    timerCallback();
}

void FullScreenTransportBar::skipPattern(int direction)
{
    const auto& data  = audioEngine.getModulePatternData();
    const auto& times = data.getOrderStartTimes();
    const auto& order = data.getOrder();
    const auto pos = audioEngine.getPatternPlayPosition();
    if (times.empty() || ! pos.isValid() || pos.orderPosition < 0 || pos.orderPosition >= (int) times.size())
        return;

    auto playable = [&](int o) { return order[(size_t) o] >= 0 && times[(size_t) o] >= 0.0; };

    int target = -1;
    if (direction < 0 && pos.row >= 4 && playable(pos.orderPosition))
        target = pos.orderPosition; // restart this pattern first
    else
        for (int o = pos.orderPosition + direction; o >= 0 && o < (int) times.size(); o += direction)
        {
            if (order[(size_t) o] == ModulePatternData::kOrderEnd) break;
            if (playable(o)) { target = o; break; }
        }

    if (target >= 0)
    {
        audioEngine.setCurrentPosition(times[(size_t) target]);
        timerCallback();
    }
}

void FullScreenTransportBar::updatePlayPauseShape()
{
    const bool playing = audioEngine.isPlaying();
    if (playing == showingPause) return;
    showingPause = playing;
    playPauseButton->setShape(playing ? pauseIcon() : playIcon(), false, true, false);
}

void FullScreenTransportBar::timerCallback()
{
    updatePlayPauseShape();

    const double duration = audioEngine.getTotalDuration();
    const double now      = audioEngine.getCurrentPlaybackTime();
    const bool seekable   = duration > 0.0;

    backButton->setEnabled(seekable);
    forwardButton->setEnabled(seekable);
    const bool patternSkip = seekable && ! audioEngine.getModulePatternData().getOrderStartTimes().empty();
    prevPatternButton->setEnabled(patternSkip);
    nextPatternButton->setEnabled(patternSkip);
    positionSlider.setEnabled(seekable);

    if (! draggingSlider)
        positionSlider.setValue(seekable ? juce::jlimit(0.0, 1.0, now / duration) : 0.0,
                                juce::dontSendNotification);

    elapsedLabel.setText(formatTime(draggingSlider ? positionSlider.getValue() * duration : now),
                         juce::dontSendNotification);
    totalLabel.setText(seekable ? formatTime(duration) : juce::String("--:--"), juce::dontSendNotification);

    const auto* item = playlistManager.getCurrentItem();
    juce::String title = item != nullptr ? item->name : juce::String();
    if (item != nullptr && item->artist.isNotEmpty())
        title = item->artist + " - " + title;
    titleLabel.setText(title, juce::dontSendNotification);
}

void FullScreenTransportBar::paint(juce::Graphics& g)
{
    g.fillAll(barBg);
    g.setColour(separator);
    g.drawHorizontalLine(0, 0.0f, (float) getWidth());
}

void FullScreenTransportBar::resized()
{
    auto area = getLocalBounds().withTrimmedTop(1).reduced(10, 0);

    constexpr int iconSize = 18, playSize = 24, slot = 34;
    auto place = [&area](juce::Button& b, int size)
    {
        auto cell = area.removeFromLeft(slot);
        b.setBounds(cell.withSizeKeepingCentre(size, size));
    };
    auto placeText = [&area](juce::Button& b)
    {
        auto cell = area.removeFromLeft(52);
        b.setBounds(cell.withSizeKeepingCentre(46, 24));
    };
    place(*prevTrackButton, iconSize);
    placeText(*prevPatternButton);
    place(*backButton,      iconSize);
    place(*playPauseButton, playSize);
    place(*stopButton,      iconSize - 2);
    place(*forwardButton,   iconSize);
    placeText(*nextPatternButton);
    place(*nextTrackButton, iconSize);

    area.removeFromLeft(12);

    // Title gets up to a third of what's left, the seek bar the rest.
    auto title = area.removeFromRight(juce::jmin(420, area.getWidth() / 3));
    titleLabel.setBounds(title.reduced(8, 0));

    elapsedLabel.setBounds(area.removeFromLeft(64));
    totalLabel.setBounds(area.removeFromRight(64));
    positionSlider.setBounds(area.reduced(6, 0));
}
