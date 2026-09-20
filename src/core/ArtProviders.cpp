#include "ArtProviders.h"
#include "Http.h"
#include "Json.h"
#include <cctype>
#include <chrono>
#include <mutex>
#include <thread>

namespace amp {

namespace {

// MusicBrainz asks for at most one request per second per client.
void MusicBrainzThrottle()
{
    static std::mutex mutex;
    static std::chrono::steady_clock::time_point last;
    std::lock_guard<std::mutex> lock(mutex);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
    if (elapsed < 1100)
        std::this_thread::sleep_for(std::chrono::milliseconds(1100 - elapsed));
    last = std::chrono::steady_clock::now();
}

std::string Quote(const std::string& text)
{
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    out += "\"";
    return out;
}

} // namespace

std::vector<std::string> ArtProviders::Lookup(const ArtSource& source, const ArtRequest& request,
    std::vector<std::string>& downloadHeaders)
{
    downloadHeaders.clear();
    if (source.id == "deezer")
        return Deezer(request);
    if (source.id == "musicbrainz")
        return MusicBrainz(request);
    if (source.id == "theaudiodb")
        return TheAudioDB(source.apiKey.empty() ? "2" : source.apiKey, request);
    if (source.id == "discogs")
        return Discogs(source.apiKey, request, downloadHeaders);
    return {};
}

namespace {

// loose comparison of names: lower-case, alphanumerics only
std::string Squash(const std::string& text)
{
    std::string out;
    for (unsigned char c : text)
        if (c >= 128 || isalnum(c))
            out.push_back(c < 128 ? (char)tolower(c) : (char)c);
    return out;
}

bool NamesMatch(const std::string& wanted, const std::string& found)
{
    std::string a = Squash(wanted), b = Squash(found);
    if (a.empty() || b.empty())
        return false;
    return a == b || a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}

} // namespace

// Deezer's public search API needs no key, answers in a few hundred milliseconds and has a
// generous rate limit, which makes it the quickest way to fill a screen of covers.
std::vector<std::string> ArtProviders::Deezer(const ArtRequest& request)
{
    std::vector<std::string> urls;
    bool various = Squash(request.artist) == "variousartists" || Squash(request.artist) == "various";
    std::string url;
    if (request.artistImage)
        url = "https://api.deezer.com/search/artist?limit=3&q=" + Http::UrlEncode(request.artist);
    else if (various)
        url = "https://api.deezer.com/search/album?limit=5&q=" + Http::UrlEncode("album:" + Quote(request.album));
    else
        url = "https://api.deezer.com/search/album?limit=5&q="
            + Http::UrlEncode("artist:" + Quote(request.artist) + " album:" + Quote(request.album));
    HttpResponse response = Http::Get(url, {"Accept: application/json"}, 15);
    if (!response.ok())
        return urls;
    Json reply = Json::parse(response.body, nullptr, false);
    if (!reply.is_object() || !reply.contains("data") || !reply["data"].is_array())
        return urls;
    for (const Json& item : reply["data"]) {
        if (!item.is_object())
            continue;
        std::string picture;
        if (request.artistImage) {
            if (!item.contains("name") || !item["name"].is_string() || !NamesMatch(request.artist, item["name"].get<std::string>()))
                continue;
            if (item.contains("picture_big") && item["picture_big"].is_string())
                picture = item["picture_big"].get<std::string>();
            if (picture.find("/artist//") != std::string::npos)
                picture.clear(); // Deezer's grey placeholder for artists without a picture
        } else {
            if (!item.contains("title") || !item["title"].is_string() || !NamesMatch(request.album, item["title"].get<std::string>()))
                continue;
            if (!various && item.contains("artist") && item["artist"].is_object() && item["artist"].contains("name")
                && item["artist"]["name"].is_string() && !NamesMatch(request.artist, item["artist"]["name"].get<std::string>()))
                continue;
            if (item.contains("cover_big") && item["cover_big"].is_string())
                picture = item["cover_big"].get<std::string>();
            if (picture.find("/cover//") != std::string::npos)
                picture.clear();
        }
        if (!picture.empty()) {
            urls.push_back(picture);
            break;
        }
    }
    return urls;
}

std::vector<std::string> ArtProviders::MusicBrainz(const ArtRequest& request)
{
    std::vector<std::string> urls;
    if (request.artistImage || request.album.empty())
        return urls; // MusicBrainz itself hosts no artist images
    MusicBrainzThrottle();
    std::string query = "release:" + Quote(request.album) + " AND artist:" + Quote(request.artist);
    std::string url = "https://musicbrainz.org/ws/2/release/?fmt=json&limit=5&query=" + Http::UrlEncode(query);
    HttpResponse response = Http::Get(url, {"Accept: application/json"}, 20);
    if (!response.ok())
        return urls;
    Json reply = Json::parse(response.body, nullptr, false);
    if (!reply.is_object())
        return urls;
    int count = 0;
    for (const Json& release : reply.value("releases", Json::array())) {
        if (!release.is_object())
            continue;
        std::string id = release.value("id", "");
        if (id.empty())
            continue;
        urls.push_back("https://coverartarchive.org/release/" + id + "/front-500");
        if (++count >= 3)
            break;
    }
    // also try the release group, which often has art when individual releases do not
    for (const Json& release : reply.value("releases", Json::array())) {
        if (!release.is_object())
            continue;
        Json group = release.value("release-group", Json());
        if (group.is_object() && !group.value("id", "").empty()) {
            urls.push_back("https://coverartarchive.org/release-group/" + group.value("id", "") + "/front-500");
            break;
        }
    }
    return urls;
}

std::vector<std::string> ArtProviders::TheAudioDB(const std::string& apiKey, const ArtRequest& request)
{
    std::vector<std::string> urls;
    std::string base = "https://www.theaudiodb.com/api/v1/json/" + apiKey + "/";
    std::string url;
    if (request.artistImage)
        url = base + "search.php?s=" + Http::UrlEncode(request.artist);
    else
        url = base + "searchalbum.php?s=" + Http::UrlEncode(request.artist) + "&a=" + Http::UrlEncode(request.album);
    HttpResponse response = Http::Get(url, {"Accept: application/json"}, 20);
    if (!response.ok())
        return urls;
    Json reply = Json::parse(response.body, nullptr, false);
    if (!reply.is_object())
        return urls;
    Json list = reply.value(request.artistImage ? "artists" : "album", Json());
    if (!list.is_array())
        return urls;
    for (const Json& item : list) {
        if (!item.is_object())
            continue;
        const char* fields[] = {"strArtistThumb", "strArtistFanart", "strAlbumThumb", "strAlbumThumbHQ", nullptr};
        for (int i = 0; fields[i]; i++) {
            if (item.contains(fields[i]) && item[fields[i]].is_string()) {
                std::string value = item[fields[i]].get<std::string>();
                if (!value.empty())
                    urls.push_back(value + "/preview");
            }
        }
        if (!urls.empty())
            break;
    }
    return urls;
}

std::vector<std::string> ArtProviders::Discogs(const std::string& token, const ArtRequest& request,
    std::vector<std::string>& downloadHeaders)
{
    std::vector<std::string> urls;
    std::vector<std::string> headers = {"Accept: application/json"};
    if (!token.empty())
        headers.push_back("Authorization: Discogs token=" + token);
    std::string url;
    if (request.artistImage)
        url = "https://api.discogs.com/database/search?type=artist&per_page=3&q=" + Http::UrlEncode(request.artist);
    else
        url = "https://api.discogs.com/database/search?type=release&per_page=3&artist=" + Http::UrlEncode(request.artist)
            + "&release_title=" + Http::UrlEncode(request.album);
    HttpResponse response = Http::Get(url, headers, 20);
    if (!response.ok())
        return urls;
    Json reply = Json::parse(response.body, nullptr, false);
    if (!reply.is_object())
        return urls;
    for (const Json& item : reply.value("results", Json::array())) {
        if (!item.is_object())
            continue;
        std::string cover = item.value("cover_image", "");
        if (cover.empty() || cover.find("spacer.gif") != std::string::npos)
            continue;
        urls.push_back(cover);
        if (urls.size() >= 2)
            break;
    }
    if (!token.empty())
        downloadHeaders.push_back("Authorization: Discogs token=" + token);
    return urls;
}

} // namespace amp
