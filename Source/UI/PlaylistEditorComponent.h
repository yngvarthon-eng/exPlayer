#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Playlist/PlaylistManager.h"
#include "DriveAwareFileBrowser.h"

class PlaylistEditorComponent : public juce::Component,
                               public juce::ChangeListener,
                               public juce::TableListBoxModel
{
public:
    PlaylistEditorComponent(PlaylistManager* playlist);
    ~PlaylistEditorComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ChangeListener
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    void deleteKeyPressed(int lastRowSelected) override;

    // The row the user has highlighted in the table, or -1. This only changes
    // when the user clicks, so it can differ from the playing track.
    int getSelectedRow() const { return playlistTable->getSelectedRow(); }

private:
    void addFilesFromDialog();
    void addStreamFromDialog();
    void savePlaylistDialog();
    void loadPlaylistDialog();
    void updateTableSelection();

    PlaylistManager* playlistManager;
    std::unique_ptr<juce::TableListBox> playlistTable;
    std::unique_ptr<juce::TextButton> addButton;
    std::unique_ptr<juce::TextButton> addStreamButton;
    std::unique_ptr<juce::TextButton> removeButton;
    std::unique_ptr<juce::TextButton> clearButton;
    std::unique_ptr<juce::TextButton> saveButton;
    std::unique_ptr<juce::TextButton> loadButton;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Add Files uses its own browser/dialog (rather than juce::FileChooser) so it
    // can list mounted drives alongside the default roots.
    struct AddFilesDialog
    {
        AddFilesDialog(const juce::String& filters, const juce::File& startDir);

        juce::WildcardFileFilter filter;
        DriveAwareFileBrowser browser;
        juce::FileChooserDialogBox dialogBox;
    };
    std::unique_ptr<AddFilesDialog> addFilesDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistEditorComponent)
};
