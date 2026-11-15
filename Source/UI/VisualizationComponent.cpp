#include "VisualizationComponent.h"
#include "../Visualization/Plugins/WaveformPlugin.h"
#include "../Visualization/Plugins/SpectrumBarsPlugin.h"
#include "../Visualization/Plugins/SpectrogramPlugin.h"
#include "../Visualization/Plugins/VUMeterPlugin.h"
#include "../Visualization/Plugins/LissajousPlugin.h"
#include "../Visualization/Plugins/RadialSpectrumPlugin.h"
#include "../Visualization/Plugins/ParticleBloomPlugin.h"
#include "../Visualization/Plugins/BarsPeakHoldPlugin.h"
#include "../Visualization/Plugins/StarfieldTunnelPlugin.h"
#include "../Visualization/Plugins/AlbumArtPulsePlugin.h"
#include <cmath>
#include <iterator>

VisualizationComponent::VisualizationComponent(AudioEngine* engine)
    : audioEngine(engine)
{
    plugins.add(new WaveformPlugin());
    plugins.add(new SpectrumBarsPlugin());
    plugins.add(new SpectrogramPlugin());
    plugins.add(new VUMeterPlugin());
    plugins.add(new LissajousPlugin());
    plugins.add(new RadialSpectrumPlugin());
    plugins.add(new ParticleBloomPlugin());
    plugins.add(new BarsPeakHoldPlugin());
    plugins.add(new StarfieldTunnelPlugin());
    plugins.add(new AlbumArtPulsePlugin());

    startTimer(40); // ~25 FPS
}

VisualizationComponent::~VisualizationComponent()
{
    stopTimer();
}

juce::Rectangle<int> VisualizationComponent::pluginArea() const
{
    return getLocalBounds().withTrimmedTop(kTabH);
}

void VisualizationComponent::drawTabs(juce::Graphics& g) const
{
    auto tabBar = getLocalBounds().removeFromTop(kTabH);

    // Dark background for the tab strip
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRect(tabBar);

    const int n = plugins.size();
    if (n == 0) return;

    const int tabW = tabBar.getWidth() / n;

    for (int i = 0; i < n; ++i)
    {
        auto tab = tabBar.withLeft(i * tabW).withWidth(tabW);

        bool active = (i == activePlugin);
        // Active tab: bright, others: dim
        g.setColour(active ? juce::Colour(0xff2a7abf) : juce::Colour(0xff333333));
        g.fillRect(tab.reduced(1, 0));

        // Tab label
        g.setColour(active ? juce::Colours::white : juce::Colours::grey);
        g.setFont(juce::Font(11.0f, juce::Font::plain));
        g.drawText(plugins[i]->getName(), tab, juce::Justification::centred, true);

        // Separator line between tabs
        g.setColour(juce::Colours::black);
        g.drawVerticalLine(tab.getRight(), (float)tab.getY(), (float)tab.getBottom());
    }

    // Bottom border of tab bar
    g.setColour(juce::Colour(0xff2a7abf));
    g.drawHorizontalLine(tabBar.getBottom() - 1,
                         (float)tabBar.getX(), (float)tabBar.getRight());
}

void VisualizationComponent::setAnimationSuspended(bool suspended)
{
    if (suspended)
        stopTimer();
    else if (! isTimerRunning())
        startTimer(40);
}

void VisualizationComponent::timerCallback()
{
    if (!audioEngine->tryLockAudioBuffer())
        return;

    auto& buffer = audioEngine->getAudioBuffer();
    int numSamples = buffer.getNumSamples();

    if (numSamples > 0)
        spectrumAnalyzer.analyze(buffer.getReadPointer(0), numSamples);

    if (activePlugin >= 0 && activePlugin < plugins.size())
        plugins[activePlugin]->update(buffer, spectrumAnalyzer.getSpectrumData());

    audioEngine->unlockAudioBuffer();

    repaint();
}

void VisualizationComponent::paint(juce::Graphics& g)
{
    // Draw the plugin visualizer in the area below the tab bar
    auto area = pluginArea();
    if (activePlugin >= 0 && activePlugin < plugins.size())
        plugins[activePlugin]->render(g, area);
    else
        g.fillRect(area); // black already from fillAll

    // Draw tab bar on top (so it's always visible)
    drawTabs(g);
}

void VisualizationComponent::resized()
{
}

juce::String VisualizationComponent::getPluginName(int index) const
{
    return juce::isPositiveAndBelow(index, plugins.size()) ? plugins[index]->getName() : juce::String();
}

void VisualizationComponent::setActivePluginIndex(int index)
{
    if (juce::isPositiveAndBelow(index, plugins.size()) && index != activePlugin)
    {
        activePlugin = index;
        repaint();
    }
}

bool VisualizationComponent::handleDigitHotkey(const juce::KeyPress& key)
{
    auto c = key.getTextCharacter();
    if (c < '1' || c > '9')
        return false;

    int index = (int)(c - '1');
    if (!juce::isPositiveAndBelow(index, plugins.size()))
        return false;

    setActivePluginIndex(index);
    return true;
}

std::unique_ptr<VisualizationPlugin> VisualizationComponent::createPluginInstance(int index) const
{
    // Mirrors the plugin list built in the constructor above.
    switch (index)
    {
        case 0: return std::make_unique<WaveformPlugin>();
        case 1: return std::make_unique<SpectrumBarsPlugin>();
        case 2: return std::make_unique<SpectrogramPlugin>();
        case 3: return std::make_unique<VUMeterPlugin>();
        case 4: return std::make_unique<LissajousPlugin>();
        case 5: return std::make_unique<RadialSpectrumPlugin>();
        case 6: return std::make_unique<ParticleBloomPlugin>();
        case 7: return std::make_unique<BarsPeakHoldPlugin>();
        case 8: return std::make_unique<StarfieldTunnelPlugin>();
        case 9: return std::make_unique<AlbumArtPulsePlugin>();
        default: return nullptr;
    }
}

std::function<std::unique_ptr<VisualizationPlugin>()> VisualizationComponent::makePluginFactory(int index) const
{
    const juce::Image albumArtSnapshot = albumArtImage;
    const float anchorSnapshot = albumArtVerticalAnchor;
    const juce::Colour colourSnapshot = albumArtWaveformColour;
    return [this, index, albumArtSnapshot, anchorSnapshot, colourSnapshot]
    {
        auto plugin = createPluginInstance(index);
        if (auto* pulse = dynamic_cast<AlbumArtPulsePlugin*>(plugin.get()))
        {
            pulse->setImage(albumArtSnapshot);
            pulse->setVerticalAnchor(anchorSnapshot);
            pulse->setWaveformColour(colourSnapshot);
        }
        return plugin;
    };
}

void VisualizationComponent::chooseAlbumArtImage()
{
    albumArtFileChooser = std::make_unique<juce::FileChooser>(
        "Choose a picture to pulse to the beat",
        juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
        "*.png;*.jpg;*.jpeg;*.bmp;*.gif");

    albumArtFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            auto loaded = juce::ImageFileFormat::loadFrom(file);
            if (! loaded.isValid())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Album Art Pulse", "Could not load image: " + file.getFileName());
                return;
            }

            setAlbumArtImage(loaded);
        });
}

void VisualizationComponent::setAlbumArtImage(const juce::Image& newImage)
{
    albumArtImage = newImage;

    for (auto* p : plugins)
        if (auto* pulse = dynamic_cast<AlbumArtPulsePlugin*>(p))
            pulse->setImage(albumArtImage);

    repaint();
}

void VisualizationComponent::setAlbumArtVerticalAnchor(float anchor)
{
    albumArtVerticalAnchor = juce::jlimit(0.0f, 1.0f, anchor);

    for (auto* p : plugins)
        if (auto* pulse = dynamic_cast<AlbumArtPulsePlugin*>(p))
            pulse->setVerticalAnchor(albumArtVerticalAnchor);

    repaint();
}

void VisualizationComponent::setAlbumArtWaveformColour(juce::Colour colour)
{
    albumArtWaveformColour = colour;

    for (auto* p : plugins)
        if (auto* pulse = dynamic_cast<AlbumArtPulsePlugin*>(p))
            pulse->setWaveformColour(albumArtWaveformColour);

    repaint();
}

void VisualizationComponent::chooseAlbumArtWaveformColour()
{
    struct ColourOption { const char* name; juce::Colour colour; };
    static const ColourOption options[] = {
        { "White",   juce::Colours::white },
        { "Lime",    juce::Colours::lime },
        { "Cyan",    juce::Colours::cyan },
        { "Yellow",  juce::Colours::yellow },
        { "Magenta", juce::Colours::magenta },
        { "Orange",  juce::Colours::orange },
        { "Red",     juce::Colours::red },
    };

    juce::PopupMenu menu;
    for (int i = 0; i < (int) std::size(options); ++i)
        menu.addItem(i + 1, options[i].name, true, options[i].colour == albumArtWaveformColour);

    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this](int result)
        {
            if (result <= 0 || result > (int) std::size(options))
                return;

            setAlbumArtWaveformColour(options[result - 1].colour);
        });
}

void VisualizationComponent::mouseDown(const juce::MouseEvent& e)
{
    // Check if click is in the tab bar
    if (e.y < kTabH)
    {
        if (plugins.size() == 0)
            return;

        int tabW = getWidth() / plugins.size();
        if (tabW <= 0) return;

        int clicked = e.x / tabW;
        clicked = juce::jlimit(0, plugins.size() - 1, clicked);
        if (clicked != activePlugin)
        {
            activePlugin = clicked;
            repaint();
        }
        return;
    }

    if (! (juce::isPositiveAndBelow(activePlugin, plugins.size())
           && dynamic_cast<AlbumArtPulsePlugin*>(plugins[activePlugin]) != nullptr))
        return;

    // Right-click anywhere in the tab picks the waveform overlay's colour,
    // regardless of drag/click state below.
    if (e.mods.isPopupMenu())
    {
        chooseAlbumArtWaveformColour();
        return;
    }

    // No picture yet: nothing to drag, so a left click always opens the chooser.
    if (! albumArtImage.isValid())
    {
        chooseAlbumArtImage();
        return;
    }

    // A picture is already loaded: don't open the chooser immediately -- wait to
    // see in mouseUp whether this was a click (replace the picture) or a drag
    // (reposition it), so dragging doesn't also pop a file dialog.
    draggingAlbumArt = true;
    albumArtDragStartY = e.y;
    albumArtDragStartAnchor = albumArtVerticalAnchor;
}

void VisualizationComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (! draggingAlbumArt)
        return;

    const int areaHeight = juce::jmax(1, pluginArea().getHeight());
    const float deltaAnchor = (float) (e.y - albumArtDragStartY) / (float) areaHeight;
    setAlbumArtVerticalAnchor(albumArtDragStartAnchor + deltaAnchor);
}

void VisualizationComponent::mouseUp(const juce::MouseEvent& e)
{
    if (! draggingAlbumArt)
        return;

    draggingAlbumArt = false;

    // Barely moved -- treat it as a click rather than a drag, same gesture that
    // used to unconditionally open the chooser before dragging existed.
    if (std::abs(e.y - albumArtDragStartY) < 4)
        chooseAlbumArtImage();
}
