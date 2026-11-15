#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"
#include "ReservesTopRightCorner.h"

// Generic kiosk-mode top-level window that temporarily reparents an existing
// component full screen, and puts it back where it came from (same parent,
// same bounds) when the user presses Escape. Unlike FullScreenVisualizerWindow
// (which owns a fresh child), this hosts a component that is fed by, and
// keeps its state in, its normal owner -- e.g. the pattern/sequencer viewer,
// whose playback position is pushed in from MainComponent's timer. Also
// answers the shared transport shortcuts (space/left/right/backspace).
class FullScreenHostWindow : public juce::Component,
                             private juce::KeyListener
{
public:
    // `content` must outlive this window (it always does here: MainComponent
    // owns both the content and, indirectly via the onClosed callback, this
    // window's lifetime).
    // `hideCursor` blanks the mouse pointer while this window is up -- used
    // for video playback, where there's nothing to point at and transport is
    // all keyboard shortcuts; left off for the sequencer view, which still
    // has clickable pattern cells.
    FullScreenHostWindow(juce::Component& content, AudioEngine* engine, PlaylistManager* playlist,
                         std::function<void()> onClosed, bool hideCursor = false);
    ~FullScreenHostWindow() override;

    // Adds this to the desktop, enters kiosk mode, reparents content in, and
    // grabs keyboard focus.
    void show();

    // Optional panel shown to the right of the hosted content, at a fixed
    // share of the window's width -- fixed so it doesn't jump about as a
    // playlist moves between tracks of different shapes. The border between
    // the two can be dragged to change the share (kept between
    // kMinSidePanelWidth and kMaxSidePanelShare); Tab shows/hides the panel.
    // `onSidePanelLayoutChanged` reports both, so the owner can reopen the
    // panel the same way next time. Owned by the window. Call before show().
    void setSidePanel(std::unique_ptr<juce::Component> panel, float widthShare, bool initiallyShown,
                      std::function<void(bool shown, float widthShare)> onSidePanelLayoutChanged);
    juce::Component* getSidePanel() const { return sidePanel.get(); }

    // Optional strip along the bottom of the window, across its full width
    // (under the content and the side panel), e.g. transport controls. Owned
    // by the window. Call before show().
    void setBottomBar(std::unique_ptr<juce::Component> bar, int height);

    // Hook for the side panel's own keys (e.g. visualizer tab digits); tried
    // before the transport shortcuts.
    std::function<bool(const juce::KeyPress&)> onExtraKeyPress;

    static constexpr int   kMinSidePanelWidth     = 320;
    static constexpr float kMaxSidePanelShare     = 0.65f;
    static constexpr float kDefaultSidePanelShare = 0.35f;
    static constexpr int   kDividerWidth          = 6;
    static constexpr int   kExitButtonWidth   = 64;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originator) override;

private:
    // Same as pressing Escape: puts the content back and asks the owner to
    // drop this window.
    void requestClose();
    void updateTopRightReserve(juce::Component* cornerOwner);

    // Mouse-driven way out, alongside Escape. Not shown when the cursor is
    // hidden (video), since it couldn't be clicked.
    std::unique_ptr<juce::TextButton> exitButton;

    // Reparents content back to where it came from. Safe to call more than
    // once (e.g. from both keyPressed and the destructor).
    void restoreContent();

    juce::Component& content;
    AudioEngine* audioEngine;
    PlaylistManager* playlistManager;
    juce::Component* originalParent = nullptr;
    juce::Rectangle<int> originalBounds;
    bool restored = false;

    std::function<void()> onClosed;
    bool hideCursor = false;

    std::unique_ptr<juce::Component> bottomBar;
    int bottomBarHeight = 0;

    // The draggable border between the content and the side panel.
    struct SideDivider : juce::Component
    {
        explicit SideDivider(FullScreenHostWindow& o) : owner(o)
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        }
        void paint(juce::Graphics& g) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent&) override;
        FullScreenHostWindow& owner;
    };

    void setSidePanelShare(float share, bool notify);
    void notifySidePanelLayout();

    std::unique_ptr<juce::Component> sidePanel;
    std::unique_ptr<SideDivider> sideDivider;
    std::function<void(bool, float)> onSidePanelLayoutChanged;
    bool  sidePanelShown = false;
    float sidePanelShare = kDefaultSidePanelShare;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullScreenHostWindow)
};
