#include "ArtistLinks.h"
#include "ArtProviders.h"
#include "Http.h"
#include "Json.h"
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace amp {

namespace {

const int64_t kRetryAfter = 7 * 24 * 3600; // a name that found nothing is asked about again after a week

int64_t Now()
{
    return (int64_t)time(nullptr);
}

std::string LuceneQuoted(const std::string& text)
{
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out + "\"";
}

std::string StringField(const Json& object, const char* name)
{
    if (!object.is_object() || !object.contains(name) || !object[name].is_string())
        return "";
    return object[name].get<std::string>();
}

} // namespace

ArtistLinks& ArtistLinks::Shared()
{
    static ArtistLinks* shared = new ArtistLinks(); // outlives the threads that may still ask at exit
    return *shared;
}

void ArtistLinks::Open(const std::string& directory)
{
    std::lock_guard<std::mutex> lock(fMutex);
    fPath = directory + "/artist-mbids.json";
    std::ifstream file(fPath);
    if (!file)
        return;
    std::stringstream text;
    text << file.rdbuf();
    Json data = Json::parse(text.str(), nullptr, false);
    if (!data.is_object() || !data.contains("artists") || !data["artists"].is_object())
        return;
    for (auto& item : data["artists"].items()) {
        const Json& value = item.value();
        if (!value.is_object())
            continue;
        Entry entry;
        entry.mbid = StringField(value, "id");
        if (value.contains("checked") && value["checked"].is_number_integer())
            entry.checked = value["checked"].get<int64_t>();
        if (!entry.mbid.empty() && !IsMbid(entry.mbid))
            continue;
        // what was learned in this run wins over the file
        fEntries.emplace(item.key(), entry);
    }
}

void ArtistLinks::SaveLocked() const
{
    if (fPath.empty())
        return;
    Json artists = Json::object();
    for (auto& entry : fEntries)
        artists[entry.first] = {{"id", entry.second.mbid}, {"checked", entry.second.checked}};
    Json data = {{"version", 1}, {"artists", artists}};
    std::string temporary = fPath + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file)
            return;
        file << data.dump(1);
        if (!file)
            return;
    }
    rename(temporary.c_str(), fPath.c_str());
}

void ArtistLinks::Store(const std::string& artist, const Entry& entry)
{
    std::string key = ArtProviders::SquashName(artist);
    if (key.empty())
        return;
    std::lock_guard<std::mutex> lock(fMutex);
    auto found = fEntries.find(key);
    if (found != fEntries.end() && found->second.mbid == entry.mbid && !entry.mbid.empty())
        return; // nothing new
    fEntries[key] = entry;
    SaveLocked();
}

void ArtistLinks::Remember(const std::string& artist, const std::string& mbid)
{
    if (!IsMbid(mbid))
        return;
    Entry entry;
    entry.mbid = mbid;
    entry.checked = Now();
    Store(artist, entry);
}

std::string ArtistLinks::Known(const std::string& artist) const
{
    std::lock_guard<std::mutex> lock(fMutex);
    auto found = fEntries.find(ArtProviders::SquashName(artist));
    return found == fEntries.end() ? std::string() : found->second.mbid;
}

std::string ArtistLinks::Resolve(const std::string& artist, const std::string& album)
{
    if (ArtProviders::SquashName(artist).empty())
        return "";
    {
        std::lock_guard<std::mutex> lock(fMutex);
        auto found = fEntries.find(ArtProviders::SquashName(artist));
        if (found != fEntries.end() && (!found->second.mbid.empty() || Now() - found->second.checked < kRetryAfter))
            return found->second.mbid;
    }
    const std::vector<std::string> headers = {"Accept: application/json"};
    std::string mbid;
    bool answered = false;
    ArtProviders::MusicBrainzThrottle();
    HttpResponse response = Http::Get("https://musicbrainz.org/ws/2/artist/?fmt=json&limit=10&query="
        + Http::UrlEncode("artist:" + LuceneQuoted(artist)), headers, 8);
    if (response.ok()) {
        answered = true;
        mbid = PickArtist(artist, response.body);
    }
    // several artists share the name: the one credited with this album is ours
    if (mbid.empty() && answered && !album.empty()) {
        ArtProviders::MusicBrainzThrottle();
        response = Http::Get("https://musicbrainz.org/ws/2/release/?fmt=json&limit=5&query="
            + Http::UrlEncode("release:" + LuceneQuoted(album) + " AND artist:" + LuceneQuoted(artist)), headers, 8);
        if (response.ok())
            mbid = PickCreditedArtist(artist, response.body);
    }
    if (!mbid.empty())
        Remember(artist, mbid);
    else if (answered) {
        Entry nothing;
        nothing.checked = Now();
        Store(artist, nothing);
    }
    return mbid;
}

std::string ArtistLinks::ArtistUrl(const std::string& mbid)
{
    return "https://musicbrainz.org/artist/" + mbid;
}

std::string ArtistLinks::SearchUrl(const std::string& artist)
{
    return "https://musicbrainz.org/search?query=" + Http::UrlEncode(artist) + "&type=artist&method=indexed";
}

std::string ArtistLinks::PageUrl(const std::string& artist, const std::string& mbid)
{
    return IsMbid(mbid) ? ArtistUrl(mbid) : SearchUrl(artist);
}

std::string ArtistLinks::PickArtist(const std::string& artist, const std::string& reply)
{
    Json data = Json::parse(reply, nullptr, false);
    if (!data.is_object() || !data.contains("artists") || !data["artists"].is_array())
        return "";
    std::string wanted = ArtProviders::SquashName(artist), found;
    for (const Json& item : data["artists"]) {
        std::string id = StringField(item, "id");
        if (!IsMbid(id) || ArtProviders::SquashName(StringField(item, "name")) != wanted)
            continue;
        if (!found.empty() && found != id)
            return ""; // two artists of that name: a search lets the user choose
        found = id;
    }
    return found;
}

std::string ArtistLinks::PickCreditedArtist(const std::string& artist, const std::string& reply)
{
    Json data = Json::parse(reply, nullptr, false);
    if (!data.is_object() || !data.contains("releases") || !data["releases"].is_array())
        return "";
    std::string wanted = ArtProviders::SquashName(artist);
    for (const Json& release : data["releases"]) {
        if (!release.is_object() || !release.contains("artist-credit") || !release["artist-credit"].is_array())
            continue;
        for (const Json& credit : release["artist-credit"]) {
            if (!credit.is_object() || !credit.contains("artist"))
                continue;
            const Json& who = credit["artist"];
            std::string id = StringField(who, "id");
            if (IsMbid(id) && (ArtProviders::SquashName(StringField(who, "name")) == wanted
                    || ArtProviders::SquashName(StringField(credit, "name")) == wanted))
                return id;
        }
    }
    return "";
}

bool ArtistLinks::IsMbid(const std::string& text)
{
    // 8-4-4-4-12 hexadecimal digits
    if (text.size() != 36)
        return false;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return false;
        } else if (!isxdigit((unsigned char)c))
            return false;
    }
    return true;
}

} // namespace amp
