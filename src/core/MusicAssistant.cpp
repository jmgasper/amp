#include "MusicAssistant.h"
#include "Http.h"
#include <cstdio>

namespace tasamp {

namespace {

// Music Assistant sends explicit nulls for unset fields; nlohmann's value() throws on those,
// so every field is read through these type-checked helpers.
std::string Str(const Json& j, const char* key, const std::string& fallback = "")
{
    if (!j.is_object())
        return fallback;
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : fallback;
}

int64_t Int(const Json& j, const char* key, int64_t fallback = 0)
{
    if (!j.is_object())
        return fallback;
    auto it = j.find(key);
    if (it == j.end())
        return fallback;
    if (it->is_number_integer())
        return it->get<int64_t>();
    if (it->is_number_float())
        return (int64_t)it->get<double>();
    return fallback;
}

double Num(const Json& j, const char* key, double fallback = 0.0)
{
    if (!j.is_object())
        return fallback;
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<double>() : fallback;
}

bool Flag(const Json& j, const char* key, bool fallback = false)
{
    if (!j.is_object())
        return fallback;
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : fallback;
}

Json Obj(const Json& j, const char* key)
{
    if (!j.is_object())
        return Json();
    auto it = j.find(key);
    return (it != j.end() && it->is_object()) ? *it : Json();
}

Json Arr(const Json& j, const char* key)
{
    if (!j.is_object())
        return Json::array();
    auto it = j.find(key);
    return (it != j.end() && it->is_array()) ? *it : Json::array();
}

} // namespace

MusicAssistant::MusicAssistant() {}

void MusicAssistant::Configure(const std::string& host, int port, const std::string& username,
    const std::string& password, const std::string& token)
{
    std::lock_guard<std::mutex> lock(fMutex);
    fHost = host;
    fPort = port;
    fUsername = username;
    fPassword = password;
    fToken = token;
}

bool MusicAssistant::IsConfigured() const
{
    std::lock_guard<std::mutex> lock(fMutex);
    return !fHost.empty() && fPort > 0;
}

std::string MusicAssistant::BaseUrl() const
{
    std::lock_guard<std::mutex> lock(fMutex);
    std::string host = fHost;
    if (host.compare(0, 7, "http://") == 0 || host.compare(0, 8, "https://") == 0)
        return host + ":" + std::to_string(fPort);
    return "http://" + host + ":" + std::to_string(fPort);
}

std::string MusicAssistant::Token() const
{
    std::lock_guard<std::mutex> lock(fMutex);
    return fToken;
}

std::string MusicAssistant::Host() const
{
    std::lock_guard<std::mutex> lock(fMutex);
    std::string host = fHost;
    size_t scheme = host.find("://");
    if (scheme != std::string::npos)
        host = host.substr(scheme + 3);
    return host;
}

int MusicAssistant::Port() const
{
    std::lock_guard<std::mutex> lock(fMutex);
    return fPort;
}

std::vector<std::string> MusicAssistant::AuthHeaders() const
{
    std::vector<std::string> headers = {"Content-Type: application/json", "Accept: application/json"};
    std::string token = Token();
    if (!token.empty())
        headers.push_back("Authorization: Bearer " + token);
    return headers;
}

Json MusicAssistant::Post(const std::string& path, const Json& body, long& status, std::string& error, bool auth)
{
    std::vector<std::string> headers = auth ? AuthHeaders()
        : std::vector<std::string>{"Content-Type: application/json", "Accept: application/json"};
    HttpResponse response = Http::Post(BaseUrl() + path, body.dump(), headers, 60);
    status = response.status;
    if (status == 0) {
        error = response.error.empty() ? "no response" : response.error;
        return Json();
    }
    Json result = Json::parse(response.body, nullptr, false);
    if (result.is_discarded()) {
        if (!response.ok())
            error = "HTTP " + std::to_string(status) + ": " + response.body.substr(0, 200);
        else
            error = "invalid JSON reply";
        return Json();
    }
    return result;
}

bool MusicAssistant::Login(std::string& error)
{
    std::string username, password;
    {
        std::lock_guard<std::mutex> lock(fMutex);
        username = fUsername;
        password = fPassword;
    }
    if (username.empty()) {
        error = "no Music Assistant username configured";
        return false;
    }
    Json body = {{"credentials", {{"username", username}, {"password", password}}}, {"device_name", "TasAmp"}};
    long status = 0;
    Json reply = Post("/auth/login", body, status, error, false);
    if (reply.is_null())
        return false;
    if (!Flag(reply, "success")) {
        error = Str(reply, "error", "login failed");
        return false;
    }
    std::string token = Str(reply, "token");
    if (token.empty()) {
        error = "login reply without token";
        return false;
    }
    std::lock_guard<std::mutex> lock(fMutex);
    fToken = token;
    return true;
}

bool MusicAssistant::EnsureAuthenticated(std::string& error)
{
    if (!IsConfigured()) {
        error = "Music Assistant server not configured";
        return false;
    }
    if (!Token().empty()) {
        long status = 0;
        std::string ignored;
        Json me = Post("/api", Json{{"command", "auth/me"}, {"args", Json::object()}}, status, ignored, true);
        if (status == 200 && me.is_object() && (me.contains("user_id") || me.contains("username")
                || (me.contains("result") && me["result"].is_object())))
            return true;
        if (status == 0) {
            error = ignored;
            return false;
        }
    }
    return Login(error);
}

Json MusicAssistant::Command(const std::string& command, const Json& args, std::string& error)
{
    try {
        return CommandChecked(command, args, error);
    } catch (const std::exception& e) {
        error = std::string("invalid reply for ") + command + ": " + e.what();
        return Json();
    }
}

Json MusicAssistant::CommandChecked(const std::string& command, const Json& args, std::string& error)
{
    long status = 0;
    Json body = {{"command", command}, {"args", args.is_null() ? Json::object() : args}};
    Json reply = Post("/api", body, status, error, true);
    if (status == 401) {
        // token expired: log in again and retry once
        std::string loginError;
        if (!Login(loginError)) {
            error = "authentication failed: " + loginError;
            return Json();
        }
        reply = Post("/api", body, status, error, true);
    }
    if (status == 0)
        return Json();
    if (status < 200 || status >= 300) {
        std::string detail;
        if (reply.is_object()) {
            if (reply.contains("error"))
                detail = reply["error"].is_string() ? reply["error"].get<std::string>() : reply["error"].dump();
            else if (reply.contains("details"))
                detail = reply["details"].dump();
            else if (reply.contains("error_code"))
                detail = reply["error_code"].dump();
        }
        if (detail.empty())
            detail = reply.is_null() ? "" : reply.dump().substr(0, 200);
        error = "HTTP " + std::to_string(status) + " for " + command + (detail.empty() ? "" : ": " + detail);
        return Json();
    }
    if (reply.is_object() && reply.contains("result") && (reply.contains("message_id") || reply.size() <= 2))
        return reply["result"];
    if (reply.is_object() && reply.contains("error") && !reply.contains("result")) {
        error = reply["error"].is_string() ? reply["error"].get<std::string>() : reply["error"].dump();
        return Json();
    }
    return reply;
}

bool MusicAssistant::ServerInfo(Json& info, std::string& error)
{
    HttpResponse response = Http::Get(BaseUrl() + "/info", {}, 10);
    if (!response.ok()) {
        error = response.status ? "HTTP " + std::to_string(response.status) : response.error;
        return false;
    }
    info = Json::parse(response.body, nullptr, false);
    if (!info.is_object()) {
        error = "invalid /info reply";
        return false;
    }
    return true;
}

// ---- parsing helpers --------------------------------------------------------

ArtKey MusicAssistant::ImageKey(const Json& image)
{
    if (!image.is_object())
        return "";
    std::string proxy = Str(image, "proxy_id");
    if (!proxy.empty())
        return "ma:" + proxy;
    std::string path = Str(image, "path");
    if (path.empty())
        return "";
    bool remote = Flag(image, "remotely_accessible");
    if (remote && (path.compare(0, 7, "http://") == 0 || path.compare(0, 8, "https://") == 0))
        return "mau:" + path;
    return "map:" + Str(image, "provider") + "|" + path;
}

std::string MusicAssistant::UrlForKey(const ArtKey& key, int size) const
{
    if (key.compare(0, 3, "ma:") == 0)
        return BaseUrl() + "/imageproxy/" + key.substr(3) + "?size=" + std::to_string(size) + "&fmt=jpeg";
    if (key.compare(0, 4, "mau:") == 0)
        return key.substr(4);
    if (key.compare(0, 4, "map:") == 0) {
        size_t bar = key.find('|', 4);
        if (bar == std::string::npos)
            return "";
        return BaseUrl() + "/imageproxy?provider=" + Http::UrlEncode(key.substr(4, bar - 4)) + "&path="
            + Http::UrlEncode(key.substr(bar + 1)) + "&size=" + std::to_string(size) + "&fmt=jpeg";
    }
    return "";
}

std::string MusicAssistant::ImageUrl(const Json& image, int size) const
{
    if (!image.is_object())
        return "";
    std::string proxy = Str(image, "proxy_id");
    std::string path = Str(image, "path");
    std::string provider = Str(image, "provider");
    bool remote = Flag(image, "remotely_accessible");
    if (!proxy.empty())
        return BaseUrl() + "/imageproxy/" + proxy + "?size=" + std::to_string(size) + "&fmt=jpeg";
    if (remote && (path.compare(0, 7, "http://") == 0 || path.compare(0, 8, "https://") == 0))
        return path;
    if (!path.empty())
        return BaseUrl() + "/imageproxy?path=" + Http::UrlEncode(path) + "&provider=" + Http::UrlEncode(provider)
            + "&size=" + std::to_string(size) + "&fmt=jpeg";
    return "";
}

namespace {

Json FirstImage(const Json& item, const char* type = "thumb")
{
    if (!item.is_object())
        return Json();
    Json images = Arr(Obj(item, "metadata"), "images");
    Json fallback;
    for (const Json& img : images) {
        if (!img.is_object())
            continue;
        if (Str(img, "type") == type)
            return img;
        if (fallback.is_null())
            fallback = img;
    }
    if (!fallback.is_null())
        return fallback;
    return Obj(item, "image");
}

std::string JoinArtists(const Json& artists, std::string* firstUri)
{
    std::string names;
    if (!artists.is_array())
        return names;
    for (const Json& a : artists) {
        if (!a.is_object())
            continue;
        std::string name = Str(a, "name");
        if (name.empty())
            continue;
        if (!names.empty())
            names += ", ";
        names += name;
        if (firstUri && firstUri->empty())
            *firstUri = Str(a, "uri");
    }
    return names;
}

} // namespace

bool MusicAssistant::ParseAudioFormat(const Json& format, bool& lossless, int& bitrate)
{
    if (!format.is_object())
        return false;
    std::string type = ToLower(Str(format, "content_type"));
    std::string codec = ToLower(Str(format, "codec_type"));
    if (type.empty() || type == "?")
        return false;
    bitrate = (int)Int(format, "bit_rate");
    int bitDepth = (int)Int(format, "bit_depth");
    static const char* losslessTypes[] = {"flac", "wav", "alac", "aiff", "aif", "ape", "wv", "wavpack", "dsf", "dff", "tak", nullptr};
    lossless = type.compare(0, 3, "pcm") == 0;
    for (int i = 0; losslessTypes[i] && !lossless; i++)
        lossless = type == losslessTypes[i] || codec == losslessTypes[i];
    // MP4 containers hide the codec ("m4a", codec "?"): AAC never reaches these figures, ALAC does
    if (!lossless && (type == "m4a" || type == "mp4" || type == "m4b") && (bitrate >= 600 || bitDepth > 16))
        lossless = true;
    return true;
}

Track MusicAssistant::ParseTrack(const Json& item, const MusicAssistant& client)
{
    Track t;
    t.source = Source::MusicAssistant;
    t.uri = Str(item, "uri");
    t.maItemId = Str(item, "item_id");
    t.title = Str(item, "name");
    std::string artistUri;
    t.artist = JoinArtists(Arr(item, "artists"), &artistUri);
    t.maArtistUri = artistUri;
    t.durationMs = (int64_t)(Num(item, "duration") * 1000.0);
    t.trackNumber = (int)Int(item, "track_number");
    t.discNumber = (int)Int(item, "disc_number");
    Json album = Obj(item, "album");
    if (!album.is_null()) {
        t.album = Str(album, "name");
        t.maAlbumUri = Str(album, "uri");
        t.albumArtist = JoinArtists(Arr(album, "artists"), nullptr);
        t.year = (int)Int(album, "year");
    }
    if (t.albumArtist.empty())
        t.albumArtist = t.artist;
    Json genres = Arr(Obj(item, "metadata"), "genres");
    if (!genres.empty() && genres[0].is_string())
        t.genre = genres[0].get<std::string>();
    Json image = FirstImage(item);
    if (image.is_null() && !album.is_null())
        image = FirstImage(album);
    t.art = ImageKey(image);
    // quality of the best source the server knows for this track
    for (const Json& mapping : Arr(item, "provider_mappings")) {
        bool lossless = false;
        int bitrate = 0;
        if (!ParseAudioFormat(Obj(mapping, "audio_format"), lossless, bitrate))
            continue;
        if (lossless && !t.lossless) {
            t.lossless = true;
            t.bitrate = bitrate;
        } else if (!t.lossless && bitrate > t.bitrate)
            t.bitrate = bitrate;
    }
    (void)client;
    return t;
}

Album MusicAssistant::ParseAlbum(const Json& item, const MusicAssistant& client)
{
    Album a;
    a.source = Source::MusicAssistant;
    a.name = Str(item, "name");
    a.maUri = Str(item, "uri");
    a.maItemId = Str(item, "item_id");
    a.artist = JoinArtists(Arr(item, "artists"), nullptr);
    a.year = (int)Int(item, "year");
    a.sortKey = SortKeyFor(a.artist) + "|" + SortKeyFor(a.name);
    a.art = ImageKey(FirstImage(item));
    (void)client;
    return a;
}

Artist MusicAssistant::ParseArtist(const Json& item, const MusicAssistant& client)
{
    Artist a;
    a.source = Source::MusicAssistant;
    a.name = Str(item, "name");
    a.maUri = Str(item, "uri");
    a.maItemId = Str(item, "item_id");
    a.sortKey = SortKeyFor(a.name);
    a.art = ImageKey(FirstImage(item));
    (void)client;
    return a;
}

Playlist MusicAssistant::ParsePlaylist(const Json& item, const MusicAssistant& client)
{
    Playlist p;
    p.source = Source::MusicAssistant;
    p.name = Str(item, "name");
    p.maUri = Str(item, "uri");
    p.maItemId = Str(item, "item_id");
    p.editable = Flag(item, "is_editable", true);
    // read-only and dynamic server playlists are loaded on demand instead of at every sync
    p.dynamic = Flag(item, "is_dynamic") || !p.editable;
    (void)client;
    return p;
}

bool MusicAssistant::FetchPaged(const std::string& command, const Json& extraArgs, std::vector<Json>& items, std::string& error)
{
    const int limit = 500;
    int offset = 0;
    while (true) {
        Json args = extraArgs.is_object() ? extraArgs : Json::object();
        args["limit"] = limit;
        args["offset"] = offset;
        Json result = Command(command, args, error);
        if (result.is_null())
            return false;
        Json list = result;
        if (result.is_object() && result.contains("items") && result["items"].is_array())
            list = result["items"];
        if (!list.is_array()) {
            error = command + ": unexpected reply";
            return false;
        }
        for (const Json& item : list)
            items.push_back(item);
        if ((int)list.size() < limit)
            break;
        offset += limit;
        if (offset > 200000)
            break;
    }
    return true;
}

bool MusicAssistant::FetchLibrary(MASyncResult& out, const std::function<void(const std::string&)>& progress)
{
    try {
        return FetchLibraryChecked(out, progress);
    } catch (const std::exception& e) {
        out.error = std::string("unexpected data from the server: ") + e.what();
        return false;
    }
}

bool MusicAssistant::FetchLibraryChecked(MASyncResult& out, const std::function<void(const std::string&)>& progress)
{
    std::string error;
    if (!EnsureAuthenticated(error)) {
        out.error = error;
        return false;
    }
    std::vector<Json> items;
    progress("Fetching artists");
    if (!FetchPaged("music/artists/library_items", Json{{"album_artists_only", false}}, items, error)) {
        out.error = error;
        return false;
    }
    for (const Json& item : items)
        out.artists.push_back(ParseArtist(item, *this));
    items.clear();
    progress("Fetching albums");
    if (!FetchPaged("music/albums/library_items", Json::object(), items, error)) {
        out.error = error;
        return false;
    }
    for (const Json& item : items)
        out.albums.push_back(ParseAlbum(item, *this));
    items.clear();
    progress("Fetching tracks");
    if (!FetchPaged("music/tracks/library_items", Json::object(), items, error)) {
        out.error = error;
        return false;
    }
    for (const Json& item : items)
        out.tracks.push_back(ParseTrack(item, *this));
    items.clear();
    progress("Fetching playlists");
    if (!FetchPaged("music/playlists/library_items", Json::object(), items, error)) {
        out.error = error;
        return false;
    }
    for (const Json& item : items) {
        // playlist contents are fetched when a playlist is opened (see TasAmpApp::LoadMAPlaylist)
        out.playlists.push_back(ParsePlaylist(item, *this));
        out.playlistTrackUris.push_back({});
    }
    progress("Done");
    return true;
}

bool MusicAssistant::FetchPlaylistTracks(const std::string& itemId, const std::string& provider,
    std::vector<Track>& tracks, std::string& error)
{
    Json result = Command("music/playlists/playlist_tracks",
        Json{{"item_id", itemId}, {"provider_instance_id_or_domain", provider}}, error);
    if (result.is_null())
        return false;
    if (!result.is_array())
        return false;
    for (const Json& item : result) {
        if (!item.is_object() || Str(item, "media_type", "track") != "track" || Str(item, "uri").empty())
            continue;
        Track track = ParseTrack(item, *this);
        track.inLibrary = track.uri.compare(0, 10, "library://") == 0;
        tracks.push_back(track);
    }
    return true;
}

bool MusicAssistant::FetchLibraryPlaylistTracks(const std::string& libraryItemId, std::vector<Track>& tracks, std::string& error)
{
    Json playlist = Command("music/playlists/get", Json{{"item_id", libraryItemId}, {"provider_instance_id_or_domain", "library"}}, error);
    std::vector<std::pair<std::string, std::string>> mappings; // provider instance, item id
    if (playlist.is_object()) {
        // available mappings first, unavailable ones as a last resort
        for (int pass = 0; pass < 2; pass++)
            for (const Json& m : Arr(playlist, "provider_mappings")) {
                bool available = Flag(m, "available", true);
                if ((pass == 0) != available)
                    continue;
                std::string instance = Str(m, "provider_instance");
                if (instance.empty())
                    instance = Str(m, "provider_domain");
                if (!instance.empty() && !Str(m, "item_id").empty())
                    mappings.push_back({instance, Str(m, "item_id")});
            }
    }
    mappings.push_back({"library", libraryItemId});
    std::string lastError;
    for (const auto& mapping : mappings) {
        std::vector<Track> found;
        std::string mappingError;
        if (FetchPlaylistTracks(mapping.second, mapping.first, found, mappingError) && !found.empty()) {
            tracks = found;
            error.clear();
            return true;
        }
        if (!mappingError.empty())
            lastError = mappingError;
    }
    error = lastError; // empty when the playlist is simply empty
    return lastError.empty();
}

bool MusicAssistant::FetchAlbumTracks(const std::string& itemId, const std::string& provider,
    std::vector<Track>& tracks, std::string& error)
{
    Json result = Command("music/albums/album_tracks",
        Json{{"item_id", itemId}, {"provider_instance_id_or_domain", provider}}, error);
    if (!result.is_array())
        return false;
    for (const Json& item : result)
        if (item.is_object())
            tracks.push_back(ParseTrack(item, *this));
    return true;
}

bool MusicAssistant::CreatePlaylist(const std::string& name, Playlist& created, std::string& error)
{
    Json result = Command("music/playlists/create_playlist", Json{{"name", name}}, error);
    if (!result.is_object())
        return false;
    created = ParsePlaylist(result, *this);
    return !created.maItemId.empty();
}

bool MusicAssistant::AddPlaylistTracks(const std::string& dbPlaylistId, const std::vector<std::string>& uris, std::string& error)
{
    if (uris.empty())
        return true;
    Json result = Command("music/playlists/add_playlist_tracks", Json{{"db_playlist_id", dbPlaylistId}, {"uris", uris}}, error);
    return error.empty();
}

bool MusicAssistant::RemovePlaylistTracks(const std::string& dbPlaylistId, const std::vector<int>& positions, std::string& error)
{
    if (positions.empty())
        return true;
    Command("music/playlists/remove_playlist_tracks", Json{{"db_playlist_id", dbPlaylistId}, {"positions_to_remove", positions}}, error);
    return error.empty();
}

bool MusicAssistant::DeletePlaylist(const std::string& dbPlaylistId, std::string& error)
{
    Command("music/library/remove_item", Json{{"media_type", "playlist"}, {"library_item_id", dbPlaylistId}}, error);
    return error.empty();
}

bool MusicAssistant::PlayMedia(const std::string& playerId, const std::string& uri, std::string& error)
{
    Command("player_queues/play_media", Json{{"queue_id", playerId}, {"media", Json::array({uri})}, {"option", "replace"}}, error);
    return error.empty();
}

bool MusicAssistant::PlayerCommand(const std::string& command, const std::string& playerId, const Json& extra, std::string& error)
{
    Json args = extra.is_object() ? extra : Json::object();
    args["player_id"] = playerId;
    Command("players/cmd/" + command, args, error);
    return error.empty();
}

bool MusicAssistant::PlayerExists(const std::string& playerId, Json& player, std::string& error)
{
    Json result = Command("players/get", Json{{"player_id", playerId}}, error);
    if (!result.is_object() || result.empty())
        return false;
    player = result;
    return true;
}

std::string MusicAssistant::ResolvePlayerId(const std::string& clientId, std::string& error)
{
    Json player = Command("players/get", Json{{"player_id", clientId}}, error);
    if (player.is_object() && !player.empty()) {
        std::string source = Str(player, "active_source");
        if (!source.empty() && source != clientId) {
            std::string ignored;
            Json wrapper = Command("players/get", Json{{"player_id", source}}, ignored);
            if (wrapper.is_object() && !wrapper.empty())
                return source;
        }
    }
    // fall back to scanning all players for the one whose output protocol is our client
    std::string ignored;
    Json all = Command("players/all", Json::object(), ignored);
    if (all.is_array()) {
        for (const Json& p : all) {
            for (const Json& proto : Arr(p, "output_protocols"))
                if (Str(proto, "output_protocol_id") == clientId && Str(p, "player_id") != clientId)
                    return Str(p, "player_id");
        }
    }
    return clientId;
}

MAQueueState MusicAssistant::QueueState(const std::string& playerId)
{
    MAQueueState state;
    std::string error;
    Json result = Command("player_queues/get", Json{{"queue_id", playerId}}, error);
    if (!result.is_object() || result.empty())
        return state;
    state.valid = true;
    state.state = Str(result, "state");
    state.elapsed = Num(result, "elapsed_time");
    state.ended = Flag(result, "ended");
    Json current = Obj(result, "current_item");
    if (!current.is_null()) {
        state.currentName = Str(current, "name");
        state.durationMs = (int64_t)(Num(current, "duration") * 1000);
        state.currentUri = Str(Obj(current, "media_item"), "uri");
        state.qualityKnown = ParseAudioFormat(Obj(Obj(current, "streamdetails"), "audio_format"), state.lossless, state.bitrate);
    }
    return state;
}

} // namespace tasamp
