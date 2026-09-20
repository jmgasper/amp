// Music Assistant HTTP API client (login, library browsing, playlists, player control).
#pragma once
#include "Json.h"
#include "Model.h"
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace amp {

struct MASyncResult {
    std::vector<Track> tracks;
    std::vector<Album> albums;
    std::vector<Artist> artists;
    std::vector<Playlist> playlists;                    // trackIds unresolved; see playlistTrackUris
    std::vector<std::vector<std::string>> playlistTrackUris;
    std::string error;
};

struct MAQueueState {
    bool valid = false;
    std::string state;        // playing, paused, idle
    double elapsed = 0;
    std::string currentUri;
    std::string currentName;
    int64_t durationMs = 0;
    bool ended = false;
    bool qualityKnown = false;   // from the queue item's stream details
    bool lossless = false;
    int bitrate = 0;
};

class MusicAssistant {
public:
    MusicAssistant();
    void Configure(const std::string& host, int port, const std::string& username,
        const std::string& password, const std::string& token);
    bool IsConfigured() const;
    std::string BaseUrl() const;
    std::string Token() const;
    std::string Host() const;
    int Port() const;

    // Logs in with the stored credentials; on success Token() returns the new token.
    bool Login(std::string& error);
    // Validates the current token, logging in again when required.
    bool EnsureAuthenticated(std::string& error);
    // Runs an API command; returns the result JSON (null on failure, error filled).
    Json Command(const std::string& command, const Json& args, std::string& error);
    bool ServerInfo(Json& info, std::string& error);

    // Library synchronisation
    bool FetchLibrary(MASyncResult& out, const std::function<void(const std::string&)>& progress);
    bool FetchPlaylistTracks(const std::string& itemId, const std::string& provider,
        std::vector<Track>& tracks, std::string& error);
    // Resolves a library playlist to its provider mappings and fetches the tracks from the
    // first mapping that answers (the "library" pseudo provider often picks an unavailable one).
    bool FetchLibraryPlaylistTracks(const std::string& libraryItemId, std::vector<Track>& tracks, std::string& error);
    bool FetchAlbumTracks(const std::string& itemId, const std::string& provider,
        std::vector<Track>& tracks, std::string& error);

    // Playlist editing on the server
    bool CreatePlaylist(const std::string& name, Playlist& created, std::string& error);
    bool AddPlaylistTracks(const std::string& dbPlaylistId, const std::vector<std::string>& uris, std::string& error);
    bool RemovePlaylistTracks(const std::string& dbPlaylistId, const std::vector<int>& positions, std::string& error);
    bool DeletePlaylist(const std::string& dbPlaylistId, std::string& error);

    // Player / queue control for our own Sendspin player
    bool PlayMedia(const std::string& playerId, const std::string& uri, std::string& error);
    bool PlayerCommand(const std::string& command, const std::string& playerId, const Json& extra, std::string& error);
    bool PlayerExists(const std::string& playerId, Json& player, std::string& error);
    // Music Assistant wraps protocol players (our Sendspin client) in a "universal player" that
    // owns the queue; returns that player's id, or clientId when no wrapper exists.
    std::string ResolvePlayerId(const std::string& clientId, std::string& error);
    MAQueueState QueueState(const std::string& playerId);

    // Artwork
    std::string ImageUrl(const Json& image, int size) const;
    std::string UrlForKey(const ArtKey& key, int size) const;   // for "ma:", "mau:", "map:" keys
    static bool IsMAKey(const ArtKey& key) { return key.compare(0, 3, "ma:") == 0 || key.compare(0, 4, "mau:") == 0 || key.compare(0, 4, "map:") == 0; }
    std::vector<std::string> AuthHeaders() const;

    static Track ParseTrack(const Json& item, const MusicAssistant& client);
    static Album ParseAlbum(const Json& item, const MusicAssistant& client);
    static Artist ParseArtist(const Json& item, const MusicAssistant& client);
    static Playlist ParsePlaylist(const Json& item, const MusicAssistant& client);
    static ArtKey ImageKey(const Json& image);
    // Reads a Music Assistant AudioFormat object; returns false when it carries no information.
    static bool ParseAudioFormat(const Json& format, bool& lossless, int& bitrate);

private:
    Json CommandChecked(const std::string& command, const Json& args, std::string& error);
    bool FetchLibraryChecked(MASyncResult& out, const std::function<void(const std::string&)>& progress);
    Json Post(const std::string& path, const Json& body, long& status, std::string& error, bool auth);
    bool FetchPaged(const std::string& command, const Json& extraArgs, std::vector<Json>& items, std::string& error);

    mutable std::mutex fMutex;
    std::string fHost;
    int fPort = 8095;
    std::string fUsername;
    std::string fPassword;
    std::string fToken;
};

} // namespace amp
