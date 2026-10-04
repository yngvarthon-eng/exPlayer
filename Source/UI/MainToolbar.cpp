#include "MainToolbar.h"

MainToolbar::MainToolbar(AudioEngine& engine)
    : audioQuality(engine)
{
    // The five panel toggles behave identically: flip state, then let the owner
    // re-apply panel visibility and re-lay out the window.
    const std::initializer_list<juce::TextButton*> collapseButtons {
        &collapsePlaylistBtn, &collapseEqBtn, &collapseVizBtn,
        &collapseFxBtn, &collapseAnalysisBtn
    };

    for (auto* button : collapseButtons)
    {
        button->setClickingTogglesState(true);
        button->onClick = [this] { if (onLayoutChanged != nullptr) onLayoutChanged(); };
        addAndMakeVisible(*button);
    }

    fullscreenVizBtn.onClick = [this]
    {
        if (onOpenFullscreenVisualizer != nullptr)
            onOpenFullscreenVisualizer();
    };
    addAndMakeVisible(fullscreenVizBtn);

    fullscreenPatternBtn.onClick = [this]
    {
        if (onOpenFullscreenPatternViewer != nullptr)
            onOpenFullscreenPatternViewer();
    };
    fullscreenPatternBtn.setEnabled(false); // only meaningful once a module is loaded
    addAndMakeVisible(fullscreenPatternBtn);

    fullscreenVideoBtn.onClick = [this]
    {
        if (onOpenFullscreenVideo != nullptr)
            onOpenFullscreenVideo();
    };
    fullscreenVideoBtn.setEnabled(false); // only meaningful once a video is loaded
    addAndMakeVisible(fullscreenVideoBtn);

    addAndMakeVisible(audioQuality);
}

void MainToolbar::setPatternFullscreenEnabled(bool shouldEnable)
{
    fullscreenPatternBtn.setEnabled(shouldEnable);
}

void MainToolbar::setVideoFullscreenEnabled(bool shouldEnable)
{
    fullscreenVideoBtn.setEnabled(shouldEnable);
}

void MainToolbar::refreshAudioQuality()
{
    audioQuality.refreshSampleRates();
}

bool MainToolbar::isPlaylistCollapsed() const { return collapsePlaylistBtn.getToggleState(); }
bool MainToolbar::isEqCollapsed() const       { return collapseEqBtn.getToggleState(); }
bool MainToolbar::isVizCollapsed() const      { return collapseVizBtn.getToggleState(); }
bool MainToolbar::isFxCollapsed() const       { return collapseFxBtn.getToggleState(); }
bool MainToolbar::isAnalysisCollapsed() const { return collapseAnalysisBtn.getToggleState(); }

void MainToolbar::placeButton(juce::Rectangle<int>& strip, juce::Component& button, int width)
{
    button.setBounds(strip.removeFromLeft(width));
    strip.removeFromLeft(buttonGap);
}

void MainToolbar::resized()
{
    auto strip = getLocalBounds().reduced(sideMargin, verticalMargin);

    // The output quality selectors own the right hand end of the strip.
    if (strip.getWidth() > AudioQualitySelector::preferredWidth)
        audioQuality.setBounds(strip.removeFromRight(AudioQualitySelector::preferredWidth));

    placeButton(strip, collapsePlaylistBtn,  playlistButtonWidth);
    placeButton(strip, collapseEqBtn,        eqButtonWidth);
    placeButton(strip, collapseVizBtn,       vizButtonWidth);
    placeButton(strip, fullscreenVizBtn,     fullscreenButtonWidth);
    placeButton(strip, fullscreenPatternBtn, patternButtonWidth);
    placeButton(strip, fullscreenVideoBtn,   videoButtonWidth);
    placeButton(strip, collapseFxBtn,        fxButtonWidth);
    placeButton(strip, collapseAnalysisBtn,  analysisButtonWidth);
}
