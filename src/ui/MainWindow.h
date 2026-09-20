// Amp's main window: toolbar, sidebar, content views and status bar in the iTunes 8 layout.
#pragma once
#include "AlbumGridView.h"
#include "ArtistsView.h"
#include "SidebarView.h"
#include "StatusBarView.h"
#include "ToolbarView.h"
#include "TrackListView.h"
#include "player/Messages.h"
#include <CardLayout.h>
#include <SplitView.h>
#include <String.h>
#include <Window.h>
#include <set>
#include <string>

namespace amp {

class MainWindow : public BWindow {
public:
    explicit MainWindow(BRect frame);
    void MessageReceived(BMessage* message) override;
    bool QuitRequested() override;

    void SelectSource(const std::string& source, int64_t playlistId, bool play = false);
    void ShowAlbum(int64_t albumId);
    void ShowArtist(int64_t artistId);

private:
    void BuildMenu();
    void ReloadContent();
    void ReloadSidebar();
    void UpdateSummary(const std::vector<int64_t>& tracks);
    int CurrentViewMode() const { return fSource == "playlist" ? fPlaylistViewMode : fViewMode; }
    std::vector<AlbumCell> CellsFromLibrary(bool maOnly) const;
    std::vector<AlbumCell> CellsFromTracks(const std::vector<int64_t>& tracks) const;
    void ClearDrillDown();
    void UpdateNowPlaying();
    void SetViewMode(int mode);
    void PlaylistEdited(int64_t playlistId);
    std::vector<int64_t> FilterTracks(const std::vector<int64_t>& ids) const;
    void SaveGeometry();

    ToolbarView* fToolbar;
    SidebarView* fSidebar;
    BSplitView* fSplit;
    BView* fContent;
    BCardLayout* fCards;
    TrackListView* fTrackList;
    AlbumGridView* fGrid;
    ArtistsView* fArtists;
    StatusBarView* fStatus;
    std::string fSource = "music";
    int64_t fPlaylistId = 0;
    int64_t fShownAlbum = 0;
    int fViewMode = 0;
    int fPlaylistViewMode = 0;
    std::vector<int64_t> fDrillTracks;     // songs of a playlist-only album opened from the grid
    std::string fSearch;
    int64_t fNowPlayingTrack = 0;
    PlayerState fPlayerState = kStopped;
    bool fLibraryDirty = false;
    bigtime_t fLastReload = 0;
    BString fTransientStatus;
    std::set<int64_t> fLoadingPlaylists;   // MA playlists whose tracks are being fetched
    int fShownQuality = 0;                 // last stream quality pushed to the display
    BString QualityFor(int64_t trackId, int streamQuality);
};

} // namespace amp
