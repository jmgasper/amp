// Message constants shared between the player, the application and the windows.
#pragma once
#include <SupportDefs.h>

namespace tasamp {

enum {
    // player -> UI
    kMsgPlayerStateChanged = 'plst',  // "state" int32 (PlayerState), "track" int64
    kMsgPlayerProgress = 'plpr',      // "position" int64 ms, "duration" int64 ms
    kMsgPlayerError = 'pler',         // "error" string
    kMsgPlayerVolume = 'plvo',        // "volume" float
    kMsgMAStatus = 'mast',            // "connected" bool, "message" string
    kMsgTrackFinished = 'trfn',       // player -> app: "generation" int32
    // library / background -> UI
    kMsgLibraryChanged = 'lbch',
    kMsgScanProgress = 'scpr',        // "text" string, "done" bool
    kMsgArtReady = 'arrd',            // "key" string, "path" string
    kMsgMASyncDone = 'masd',          // "error" string
    kMsgPlaylistLoading = 'plld',     // "playlist" int64, "loading" bool
    // UI actions
    kMsgPlayPause = 'play',
    kMsgStop = 'stop',
    kMsgNext = 'next',
    kMsgPrevious = 'prev',
    kMsgSeek = 'seek',                // "position" int64 ms
    kMsgVolumeChanged = 'volc',       // "volume" float
    kMsgViewMode = 'view',            // "mode" int32
    kMsgSearch = 'srch',              // "text" string
    kMsgSourceSelected = 'srcs',      // "source" string, "playlist" int64
    kMsgPlayTracks = 'pltr',          // "tracks" int64[], "index" int32
    kMsgNewPlaylist = 'nwpl',
    kMsgNewPlaylistNamed = 'nwpn',    // "name" string, "tracks" int64[]
    kMsgDeletePlaylist = 'dlpl',      // "playlist" int64
    kMsgRenamePlaylist = 'rnpl',      // "playlist" int64, "name" string
    kMsgAddToPlaylist = 'adpl',       // "playlist" int64, "tracks" int64[]
    kMsgRemoveFromPlaylist = 'rmpl',  // "playlist" int64, "positions" int32[]
    kMsgMovePlaylistTracks = 'mvpl',  // "playlist" int64, "positions" int32[], "target" int32
    kMsgSyncPlaylistToMA = 'sypl',    // "playlist" int64, "enable" bool
    kMsgShowSettings = 'sett',
    kMsgSettingsChanged = 'setc',
    kMsgRescan = 'rscn',
    kMsgMAResync = 'masy',
    kMsgToggleShuffle = 'shuf',
    kMsgToggleRepeat = 'rept',
    kMsgShowAlbum = 'shal',           // "album" int64
    kMsgShowArtist = 'shar',          // "artist" int64
    kMsgShowTracks = 'shtr',          // "tracks" int64[]: open these songs as an album list
    kMsgSelectionChanged = 'selc',
    kMsgRowsDropped = 'rdrp',
    kMsgTrackDrag = 'trdg',           // dragged tracks: "tracks" int64[], "playlist" int64 (source)
};

enum PlayerState { kStopped = 0, kPlaying = 1, kPaused = 2, kLoading = 3 };

} // namespace tasamp
