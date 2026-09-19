// Settings: library folders, Music Assistant connection, artwork sources.
#pragma once
#include "core/Settings.h"
#include <Button.h>
#include <CheckBox.h>
#include <FilePanel.h>
#include <ListView.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>
#include <vector>

namespace tasamp {

class SettingsWindow : public BWindow {
public:
    explicit SettingsWindow(BWindow* parent);
    ~SettingsWindow();
    void MessageReceived(BMessage* message) override;
    bool QuitRequested() override;

private:
    void Apply();
    void RefreshFolders();
    void RefreshSources();
    void TestConnection();

    SettingsData fData;
    BListView* fFolders;
    BFilePanel* fFolderPanel = nullptr;
    BCheckBox* fMAEnabled;
    BTextControl* fMAHost;
    BTextControl* fMAPort;
    BTextControl* fMAUser;
    BTextControl* fMAPassword;
    BTextControl* fMAPlayerName;
    BStringView* fMAStatus;
    BCheckBox* fOnlineArt;
    BListView* fSources;
    std::vector<BCheckBox*> fSourceChecks;
    BTextControl* fAudioDbKey;
    BTextControl* fDiscogsToken;
    BStringView* fCacheInfo;
    BButton* fTestButton;
};

} // namespace tasamp
