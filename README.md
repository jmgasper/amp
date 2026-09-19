# TasAmp

TasAmp is a native music player for Haiku styled after iTunes 8. It plays a local library
(MP3, MP4/M4A/AAC, FLAC, WAV, OGG) and streams tracks from a
[Music Assistant](https://www.music-assistant.io) server, registering itself as a player so the
server sends audio straight to the Haiku machine.

## Features

- Local library scanned from configurable folders; tags read with TagLib, durations from the
  Media Kit when tags carry none, embedded and folder artwork picked up during the scan.
- Music Assistant: username/password login, library sync (artists, albums, tracks,
  playlists), playback through the Sendspin player protocol, playlists mirrored back to the
  server. Streamed items carry a small **MA** badge everywhere.
- Browse by song list, album-grouped list with artwork, album grid, artists, and playlists;
  instant search across title, artist and album. Playlists offer the same three views
  (list, album list, cover grid) and remember their own choice.
- Playlists: create, rename, delete, add and remove songs, drag to reorder, drag songs or
  whole albums onto a playlist in the sidebar, "Sync to Music Assistant" per playlist.
- Artwork cache with online lookup through Deezer, MusicBrainz + Cover Art Archive,
  TheAudioDB and Discogs (order and keys configurable), stored under the user cache
  directory so the next launch is instant. Whatever is visible right now is fetched first.
- Transport with volume, seek bar, shuffle and repeat; play queue with "Play Next" and
  "Add to Up Next". The display shows the playing track's artwork, title, artist and album,
  elapsed and remaining time, the MA badge for streamed tracks, and the source quality
  ("Lossless" or the bit rate).
- Library folders, the Music Assistant server and account and the artwork sources live in one
  Settings window, opened from the **Settings** button at the bottom right of the main window
  or File > Settings….

## Building on Haiku R1/beta6

```sh
pkgman install curl_devel sqlite_devel taglib2_devel
make -j6            # build-haiku/TasAmp
make package        # artifacts/tasamp-<version>-x86_64.hpkg
```

The development VM workflow (sync sources, build, screenshots) is described in
`docs/VM.md`; the code layout in `docs/ARCHITECTURE.md`.

## Music Assistant notes

TasAmp talks to Music Assistant 2.10 through its HTTP API (`POST /api`) and connects to the
authenticated `/sendspin` WebSocket proxy as an unencrypted ("transition mode") Sendspin
player, which the server allows by default (*Allow legacy clients* in the Sendspin provider
settings). Music Assistant wraps that player in a "universal player" carrying the name
configured in Settings; TasAmp sends play and queue commands to that wrapper. Only the
built-in username/password login provider is supported.

Tracks whose music provider is not loaded on the server (for example a network share that
is offline) stay in the library but cannot be played; Music Assistant answers "there is
nothing to play here" and TasAmp reports it in the status bar.

`tools/fake-ma-server.py` is a small stand-in server used to exercise the integration
without a real installation.

## License

MIT. Bundles the nlohmann JSON library (MIT, see `vendor/nlohmann`).
