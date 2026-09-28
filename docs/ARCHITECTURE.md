# Amp architecture

```
src/core     portable C++17 (no Haiku headers except in Scanner's TagLib use)
  Model.h            Track / Album / Artist / Playlist structs, art keys
  Settings           JSON settings file (library folders, MA account, art sources, UI state)
  Library            SQLite store + in-memory index (albums/artists derived from tracks)
  Scanner            folder walk, TagLib tags, embedded/folder art, optional duration probe
  ImageCache         on-disk artwork cache + fetch worker (MA proxy, online providers)
  ArtProviders       MusicBrainz/CAA, TheAudioDB, Discogs lookups
  Http               libcurl wrapper
  WebSocket          minimal RFC 6455 client
  TimeFilter         Kalman clock filter (port of the Sendspin reference)
  Sendspin           Sendspin player@v1 client (transition mode through MA's /sendspin proxy)
  MusicAssistant     MA HTTP API client: login, library, playlists, player/queue commands
  Queue              play queue with shuffle/repeat
  NetMD              MiniDisc recorder protocol: commands, secure download, title/capacity planning
  NetMDSimulator     a recorder in software (tests, AMP_NETMD_SIMULATE)
  Des                DES / two-key 3DES for NetMD
  Resampler          windowed-sinc sample rate conversion
  FlacDecoder, AlacDecoder   lossless packet decoders (Haiku's own drop the last frames)
src/player   Haiku Media Kit
  AudioOutput        BSoundPlayer + ring buffer + volume
  LocalDecoder       BMediaFile/BMediaTrack decode thread
  SendspinAudio      schedules Sendspin PCM chunks into the output at their local play time
  Player             controller: queue, engine switching, MA player registration, progress
  NetMDUsb           USB Kit transport and roster for NetMD recorders
  MiniDiscPcm        any local file -> 44.1 kHz 16-bit big-endian PCM for SP recording
  MiniDisc           MiniDiscManager: recorder state, write and erase jobs on one worker thread
src/ui       Haiku Interface Kit
  App                BApplication: owns everything, background scan/sync jobs
  MainWindow         toolbar / sidebar / content cards / status bar
  ToolbarView        transport, volume, LCD display, view switcher, search
  SidebarView        LIBRARY and PLAYLISTS sources, drop target, context menu
  TrackListView      table with album-grouped mode, sorting, selection, drag & drop
  AlbumGridView      album cover grid
  ArtistsView        artist list + header + grouped track list
  StatusBarView      +/shuffle/repeat, summary, MA indicator, Settings button
  Icons              Font Awesome glyphs: registers the bundled font, draws it centred
  SettingsWindow     Library / Music Assistant / Artwork tabs
  ArtStore           scaled BBitmap cache fed by ImageCache
  MiniDiscView       the DEVICES source: disc summary, capacity bar, tracks and pending songs
  MainWindowMiniDisc the write flow (questions, job) and its progress in the LCD and sidebar
```

MiniDisc support is described in `MINIDISC.md`.

The column header is deliberately *not* part of the scrolled table: `TrackHeaderView` is a
sibling of the `BScrollView`, so scrolling blits only the rows while the header is painted once
and stays put. `TrackListView` still owns the columns and hands them to the header through
`DrawHeader()`/`HeaderMouseDown()`, keeping it in step with `InvalidateHeader()`.

Button glyphs come from Font Awesome 6 Free. `icons::Init()` copies the font shipped in the
package into the user font directory and calls `update_font_families()`, so the family is
available on any Haiku install; `icons::Draw()` centres a glyph on its ink box, since the icons
sit on the baseline with different amounts of space around them.

## Threads and messaging

- The scanner, image cache, art store, Sendspin client, Sendspin feeder, local decoder and
  a progress ticker run on their own threads. They never touch views; they post BMessages
  (`player/Messages.h`) to the main window or the application.
- `Library` is guarded by a recursive mutex; views take a `Library::Locker` while they hold
  pointers into it. Node-based containers keep those pointers stable.
- Track completion is reported to the application looper (`kMsgTrackFinished`) so that no
  engine is ever stopped from its own thread.

## View modes

The toolbar's three buttons pick list, album list or cover grid. The library views and the
playlists each remember their own mode (`viewMode`, `playlistViewMode`).

- The album list groups *consecutive* rows of one album (`TrackListView::Relayout`), which for
  the album-ordered library is every album once, and for a playlist keeps the playlist order.
  Groups carry their own name, artist and art key because playlist tracks fetched from a
  provider have no library album.
- The grid shows `AlbumCell`s: library albums, or for a playlist the albums found in it in
  order of first appearance, each holding only the playlist's songs of that album. Opening a
  library cell uses `kMsgShowAlbum`; a playlist cell sends its songs with `kMsgShowTracks`.
- Rows of an editable playlist map to playlist positions (`fPositions`) when a search filter
  hides some of them, so remove and drag act on the right entries. Drag reordering is offered
  in the unfiltered list view only.

## Track quality

`Track::lossless` and `Track::bitrate` feed the quality label in the display. Local files get
them from TagLib (FLAC, WAV, AIFF and ALAC-in-MP4 count as lossless; a missing bit rate is
estimated from size and duration). Music Assistant tracks take them from the provider
mappings' `audio_format`; MP4 containers report codec "?" there, so a bit rate of 600 kbit/s
or more, or more than 16 bits, is read as ALAC. While a stream runs, the queue item's
`streamdetails.audio_format` overrides the library figures. `scannerVersion` in the settings
forces one full rescan when the scanner starts storing new fields.

## Artwork pipeline

`ArtStore::Get` is called from the views' `Draw` for visible items only. A miss queues a
request in `ImageCache`, at the front; asking again for a queued key moves it to the front,
and each queue is capped so items scrolled past long ago fall out. Direct downloads (Music
Assistant image proxy, remote CDN URLs) run on three workers of their own. When the server
cannot deliver a picture (HTTP 404, typically because the provider that owns the file is
offline) the request moves to the online lookup workers, which try the configured sources in
order and retry multi-artist albums with the first artist alone. Failed Music Assistant keys
are retried after 15 minutes, other misses after a week.

## Music Assistant playback

1. `App::ConnectMusicAssistant` logs in (`POST /auth/login`), stores the token, and calls
   `Player::EnableMusicAssistant`, which starts the Sendspin client.
2. The client opens `ws://host:port/sendspin`, sends `{"type":"auth","token":…}` then an
   unencrypted `client/hello` (player@v1, PCM 16-bit 44.1/48 kHz), runs the time filter and
   reports `client/state` with `available: true`.
3. Music Assistant wraps the Sendspin client (a "protocol" player) in a universal player
   that owns the queue; `MusicAssistant::ResolvePlayerId` finds it through our player's
   `active_source`. Playing an MA track calls `player_queues/play_media` for that id; the server
   streams PCM chunks (9-byte header: type 4 + big-endian int64 server timestamp) which
   `SendspinAudio` schedules through the shared `AudioOutput`. `stream/end` advances the
   Amp queue; pause/seek/stop map to `players/cmd/*`.
4. Artwork for MA items is fetched from `/imageproxy/<proxy_id>?size=512&fmt=jpeg` and cached
   like everything else.
5. Server replies carry explicit nulls, so all fields are read through the type-checked
   helpers in `MusicAssistant.cpp`; every parse path is wrapped so bad data never aborts
   the app.
6. Playlist contents are fetched when a playlist is opened (cached tracks show at once, a
   refresh runs in the background). Asking the `library` pseudo provider for the tracks can
   return nothing because the server picks the playlist's first provider mapping even when
   that instance is gone, so `FetchLibraryPlaylistTracks` reads the mappings from
   `music/playlists/get` and asks the available ones directly. Tracks that only exist on a
   provider (`tidal--…://track/…`) are stored with `inLibrary = false`: playable inside the
   playlist, hidden from the Music, Albums and Artists views.
