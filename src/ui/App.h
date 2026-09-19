// The TasAmp application: owns the library, settings, caches and the player; runs background jobs.
#pragma once
#include "ArtStore.h"
#include "core/ImageCache.h"
#include "core/Library.h"
#include "core/MusicAssistant.h"
#include "core/Scanner.h"
#include "core/Settings.h"
#include "player/Player.h"
#include <Application.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace tasamp {

class MainWindow;

class TasAmpApp : public BApplication {
public:
    TasAmpApp();
    virtual ~TasAmpApp();

    void ReadyToRun() override;
    bool QuitRequested() override;
    void MessageReceived(BMessage* message) override;
    void RefsReceived(BMessage* message) override;
    void ArgvReceived(int32 argc, char** argv) override;

    Library& GetLibrary() { return *fLibrary; }
    Settings& GetSettings() { return *fSettings; }
    ImageCache& Images() { return *fImages; }
    ArtStore& Art() { return *fArt; }
    Player& GetPlayer() { return *fPlayer; }
    MusicAssistant& MA() { return *fMA; }
    MainWindow* Window() { return fWindow; }

    void StartScan();
    void ConnectMusicAssistant(bool resync);
    void DisconnectMusicAssistant();
    void SyncPlaylistToMA(int64_t playlistId);
    // Fetches an MA playlist's tracks in the background; false when nothing was started.
    bool LoadMAPlaylist(int64_t playlistId);
    void ApplySettingsChanged();
    std::string SettingsDirectory() const { return fSettingsDir; }
    bool MASyncRunning() const { return fMASyncRunning; }

    static TasAmpApp* Instance();

private:
    void Post(uint32 what, const char* key = nullptr, const char* value = nullptr);
    void MAConnectWorker(bool resync);
    void PlaylistSyncWorker(int64_t playlistId);

    std::string fSettingsDir;
    std::string fCacheDir;
    std::unique_ptr<Settings> fSettings;
    std::unique_ptr<Library> fLibrary;
    std::unique_ptr<ImageCache> fImages;
    std::unique_ptr<MusicAssistant> fMA;
    std::unique_ptr<ArtStore> fArt;
    std::unique_ptr<Player> fPlayer;
    std::unique_ptr<Scanner> fScanner;
    MainWindow* fWindow = nullptr;
    std::atomic<bool> fMASyncRunning{false};
    std::atomic<bool> fMAConnected{false};
    std::vector<std::string> fScanFolders;
    std::mutex fMutex;
    std::set<int64_t> fPlaylistSyncQueue;
    std::set<int64_t> fPlaylistLoads;
    std::atomic<bool> fPlaylistSyncRunning{false};
};

inline TasAmpApp* App() { return TasAmpApp::Instance(); }

} // namespace tasamp
