// Amp library data model: plain structs shared by the store, the UI and the player.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace amp {

enum class Source { Local = 0, MusicAssistant = 1 };

// Identifies an image in the artwork cache. Empty key means "no artwork known".
// Keys are stable strings such as "album:<artist>|<album>" or "ma:<proxy_id>".
using ArtKey = std::string;

struct Track {
    int64_t id = 0;               // library row id
    Source source = Source::Local;
    std::string uri;              // local: absolute path; MA: "library://track/123"
    std::string title;
    std::string artist;           // display artist(s)
    std::string albumArtist;      // grouping artist (falls back to artist)
    std::string album;
    std::string genre;
    int year = 0;
    int trackNumber = 0;
    int discNumber = 0;
    int64_t durationMs = 0;
    int64_t sizeBytes = 0;
    int64_t modifiedTime = 0;     // local files: mtime (seconds)
    int bitrate = 0;              // kbit/s (0 when unknown)
    bool lossless = false;        // FLAC, WAV, ALAC, AIFF ...
    std::string maItemId;         // MA: item_id of the track ("123")
    std::string maAlbumUri;       // MA: album uri
    std::string maArtistUri;      // MA: first artist uri
    ArtKey art;                   // album art key
    int64_t albumId = 0;          // computed album row
    int64_t dateAdded = 0;
    bool inLibrary = true;        // false: MA provider item reachable only through a playlist

    bool isMA() const { return source == Source::MusicAssistant; }
    const std::string& groupingArtist() const { return albumArtist.empty() ? artist : albumArtist; }
};

struct Album {
    int64_t id = 0;
    Source source = Source::Local;
    std::string name;
    std::string artist;           // album artist
    std::string sortKey;
    int year = 0;
    std::string maUri;            // MA album uri
    std::string maItemId;
    ArtKey art;
    int trackCount = 0;
    int64_t durationMs = 0;
    std::vector<int64_t> trackIds; // in disc/track order (filled by the in-memory index)

    bool isMA() const { return source == Source::MusicAssistant; }
};

struct Artist {
    int64_t id = 0;
    Source source = Source::Local;
    std::string name;
    std::string sortKey;
    std::string maUri;
    std::string maItemId;
    ArtKey art;                   // artist image key
    int albumCount = 0;
    int trackCount = 0;
    std::vector<int64_t> albumIds;
    std::vector<int64_t> trackIds;

    bool isMA() const { return source == Source::MusicAssistant; }
};

struct Playlist {
    int64_t id = 0;
    Source source = Source::Local;   // MA playlists are read from the server
    std::string name;
    std::string maUri;               // MA playlist uri when synced/linked
    std::string maItemId;            // MA db playlist id
    bool syncToMA = false;           // local playlist that mirrors to MA
    bool editable = true;
    bool dynamic = false;            // MA playlist whose tracks are fetched on demand
    std::vector<int64_t> trackIds;   // ordered
    int64_t modified = 0;

    bool isMA() const { return source == Source::MusicAssistant; }
};

// Where a piece of artwork can be obtained; used by the image cache workers.
struct ArtRequest {
    ArtKey key;
    std::string artist;
    std::string album;         // empty for artist images
    std::string localHintPath; // a track path whose folder/tags may contain art
    std::string url;           // direct URL (MA image proxy)
    std::vector<std::string> headers;
    bool artistImage = false;
};

std::string MakeAlbumArtKey(const std::string& artist, const std::string& album);
std::string MakeArtistArtKey(const std::string& artist);
std::string FormatDuration(int64_t ms);
std::string QualityLabel(bool lossless, int bitrateKbps); // "Lossless", "320 kbps" or ""
std::string SortKeyFor(const std::string& name); // strips "The ", lowercases
std::string ToLower(const std::string& text);
bool ContainsNoCase(const std::string& haystack, const std::string& needle);

} // namespace amp
