#include "FullScreenHostWindow.h"
#include "TransportShortcuts.h"

FullScreenHostWindow::FullScreenHostWindow(juce::Component& contentToHost, AudioEngine* engine, PlaylistManager* playlist,
                                           std::function<void()> closedCallback, bool hideCursorWhenShown)
    : content(contentToHost), audioEngine(engine), playlistManager(playlist), onClosed(std::move(closedCallback)),
      hideCursor(hideCursorWhenShown)
{
    originalParent = content.getParentComponent();
    originalBounds = content.getBounds();

    setWantsKeyboardFocus(true);
    addKeyListener(this);

    if (! hideCursor)
    {
        exitButton = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xe2\x9c\x95 Exit"));
        exitButton->setTooltip("Leave fullscreen (Esc)");
        exitButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a1f24));
        exitButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        exitButton->setWantsKeyboardFocus(false);
        exitButton->onClick = [this] { requestClose(); };
        addAndMakeVisible(*exitButton);
    }
}

FullScreenHostWindow::~FullScreenHostWindow()
{
    restoreContent();
    removeKeyListener(this);

    if (juce::Desktop::getInstance().getKioskModeComponent() == this)
        juce::Desktop::getInstance().setKioskModeComponent(nullptr);

    removeFromDesktop();
}

void FullScreenHostWindow::show()
{
    addToDesktop(juce::ComponentPeer::windowIsTemporary);
    setBounds(juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->totalArea);

    addAndMakeVisible(content); // reparents content here; addChildComponent auto-removes it from its old parent
    if (exitButton != nullptr)
        exitButton->toFront(false);
    resized();

    if (hideCursor)
        setMouseCursor(juce::MouseCursor::NoCursor);

    setVisible(true);
    toFront(true);

    juce::Desktop::getInstance().setKioskModeComponent(this, false);

    grabKeyboardFocus();
}

void FullScreenHostWindow::setSidePanel(std::unique_ptr<juce::Component> panel, float widthShare,
                                        bool initiallyShown, std::function<void(bool, float)> layoutChanged)
{
    sidePanel = std::move(panel);
    onSidePanelLayoutChanged = std::move(layoutChanged);
    sidePanelShown = initiallyShown;
    sidePanelShare = juce::jlimit(0.0f, kMaxSidePanelShare, widthShare);
    if (sidePanel != nullptr)
    {
        addChildComponent(*sidePanel);
        sideDivider = std::make_unique<SideDivider>(*this);
        addChildComponent(*sideDivider);
    }
    resized();
}

void FullScreenHostWindow::setSidePanelShare(float share, bool notify)
{
    sidePanelShare = juce::jlimit(0.0f, kMaxSidePanelShare, share);
    resized();
    if (notify) notifySidePanelLayout();
}

void FullScreenHostWindow::notifySidePanelLayout()
{
    if (onSidePanelLayoutChanged) onSidePanelLayoutChanged(sidePanelShown, sidePanelShare);
}

void FullScreenHostWindow::SideDivider::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0c0c18));
    g.setColour(juce::Colour(isMouseOverOrDragging() ? 0xff4a5a7a : 0xff252540));
    const float cx = (float) getWidth() * 0.5f, cy = (float) getHeight() * 0.5f;
    for (int i = -1; i <= 1; ++i) // grip dots
        g.fillEllipse(cx - 1.5f, cy + (float) i * 8.0f - 1.5f, 3.0f, 3.0f);
}

void FullScreenHostWindow::SideDivider::mouseDrag(const juce::MouseEvent& e)
{
    // The panel's left edge follows the pointer.
    const auto total = (float) owner.getWidth();
    if (total <= 0.0f) return;
    const float pointerX = (float) e.getEventRelativeTo(&owner).x;
    const float share = (total - pointerX - (float) kDividerWidth * 0.5f) / total;
    owner.setSidePanelShare(juce::jmax(share, (float) kMinSidePanelWidth / total), false);
    repaint();
}

void FullScreenHostWindow::SideDivider::mouseUp(const juce::MouseEvent&)
{
    owner.notifySidePanelLayout();
    repaint();
}

void FullScreenHostWindow::setBottomBar(std::unique_ptr<juce::Component> bar, int height)
{
    bottomBar = std::move(bar);
    bottomBarHeight = height;
    if (bottomBar != nullptr)
        addAndMakeVisible(*bottomBar);
    resized();
}

void FullScreenHostWindow::paint(juce::Graphics& g)
{
    // Only visible if neither child covers the window (e.g. while laying out).
    g.fillAll(juce::Colour(0xff0e0e1a));
}

void FullScreenHostWindow::resized()
{
    auto area = getLocalBounds();

    if (bottomBar != nullptr)
        bottomBar->setBounds(area.removeFromBottom(bottomBarHeight));

    if (sidePanel != nullptr)
    {
        const int total = area.getWidth();
        const int maxSide = (int) ((float) total * kMaxSidePanelShare);
        const int sideW = juce::jmin(maxSide, juce::jmax(kMinSidePanelWidth, juce::roundToInt(sidePanelShare * (float) total)));

        // A screen too narrow for even the minimum panel next to the content
        // just doesn't get one.
        const bool show = sidePanelShown && sideW >= kMinSidePanelWidth;
        sidePanel->setVisible(show);
        sideDivider->setVisible(show);
        if (show)
        {
            sidePanel->setBounds(area.removeFromRight(sideW));
            sideDivider->setBounds(area.removeFromRight(kDividerWidth));
        }
    }

    // The content is only reparented in by show(); before that it still
    // belongs to its normal parent and must not be moved.
    if (content.getParentComponent() == this)
        content.setBounds(area);

    if (exitButton != nullptr)
    {
        // Top-right corner, inside whichever component's top bar is there.
        exitButton->setBounds(getWidth() - kExitButtonWidth - 4, 3, kExitButtonWidth, 22);
        updateTopRightReserve(sidePanel != nullptr && sidePanel->isVisible() ? sidePanel.get() : &content);
    }
}

void FullScreenHostWindow::updateTopRightReserve(juce::Component* cornerOwner)
{
    for (auto* c : { static_cast<juce::Component*>(&content), static_cast<juce::Component*>(sidePanel.get()) })
        if (auto* r = dynamic_cast<ReservesTopRightCorner*>(c))
            r->setTopRightReserve(c == cornerOwner ? kExitButtonWidth + 8 : 0);
}

void FullScreenHostWindow::requestClose()
{
    restoreContent();

    // Defer: we must not delete this window from inside its own event dispatch.
    if (onClosed)
    {
        auto callback = onClosed;
        juce::MessageManager::callAsync([callback] { callback(); });
    }
}

void FullScreenHostWindow::restoreContent()
{
    if (restored)
        return;
    restored = true;

    if (originalParent != nullptr)
    {
        originalParent->addAndMakeVisible(content);
        content.setBounds(originalBounds);
    }
    if (auto* r = dynamic_cast<ReservesTopRightCorner*>(&content))
        r->setTopRightReserve(0);
}

bool FullScreenHostWindow::keyPressed(const juce::KeyPress& key, juce::Component* /*originator*/)
{
    if (key == juce::KeyPress::escapeKey)
    {
        requestClose();
        return true;
    }

    if (key == juce::KeyPress::tabKey && sidePanel != nullptr)
    {
        sidePanelShown = ! sidePanelShown;
        resized();
        notifySidePanelLayout();
        return true;
    }

    if (onExtraKeyPress && onExtraKeyPress(key))
        return true;

    return handleTransportKeyPress(key, audioEngine, playlistManager);
}
