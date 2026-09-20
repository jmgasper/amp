#include "Settings.h"
#include "Json.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

namespace amp {

Settings::Settings(const std::string& path)
    : fPath(path)
{
    fData.artSources = DefaultArtSources();
}

std::vector<ArtSource> Settings::DefaultArtSources()
{
    return {{"deezer", true, ""}, {"musicbrainz", true, ""}, {"theaudiodb", true, "2"}, {"discogs", true, ""}};
}

bool Settings::Load()
{
    std::ifstream in(fPath);
    if (!in)
        return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    Json j = Json::parse(buffer.str(), nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return false;
    std::lock_guard<std::mutex> lock(fMutex);
    SettingsData d;
    d.artSources.clear();
    for (auto& f : j.value("libraryFolders", Json::array()))
        if (f.is_string())
            d.libraryFolders.push_back(f.get<std::string>());
    Json ma = j.value("musicAssistant", Json::object());
    d.maEnabled = ma.value("enabled", false);
    d.maHost = ma.value("host", "");
    d.maPort = ma.value("port", 8095);
    d.maUsername = ma.value("username", "");
    d.maPassword = ma.value("password", "");
    d.maToken = ma.value("token", "");
    d.maPlayerId = ma.value("playerId", "");
    d.maPlayerName = ma.value("playerName", "Amp");
    Json art = j.value("artwork", Json::object());
    d.fetchOnlineArt = art.value("fetchOnline", true);
    for (auto& s : art.value("sources", Json::array())) {
        ArtSource src;
        src.id = s.value("id", "");
        src.enabled = s.value("enabled", true);
        src.apiKey = s.value("apiKey", "");
        if (!src.id.empty())
            d.artSources.push_back(src);
    }
    // make sure every known source is present (new versions may add sources)
    for (const ArtSource& def : DefaultArtSources()) {
        bool found = false;
        for (const ArtSource& s : d.artSources)
            if (s.id == def.id)
                found = true;
        if (!found) {
            if (def.id == "deezer")
                d.artSources.insert(d.artSources.begin(), def); // the quick source goes first
            else
                d.artSources.push_back(def);
        }
    }
    Json ui = j.value("ui", Json::object());
    d.volume = ui.value("volume", 0.8f);
    d.viewMode = ui.value("viewMode", 0);
    d.playlistViewMode = ui.value("playlistViewMode", 0);
    d.selectedSource = ui.value("selectedSource", "music");
    d.shuffle = ui.value("shuffle", false);
    d.repeat = ui.value("repeat", 0);
    d.windowX = ui.value("windowX", 60);
    d.windowY = ui.value("windowY", 60);
    d.windowW = ui.value("windowW", 1180);
    d.windowH = ui.value("windowH", 720);
    d.sidebarWidth = ui.value("sidebarWidth", 200);
    d.scannerVersion = j.value("scannerVersion", 0);
    fData = d;
    return true;
}

bool Settings::Save()
{
    Json j;
    SettingsData d;
    {
        std::lock_guard<std::mutex> lock(fMutex);
        d = fData;
    }
    j["libraryFolders"] = d.libraryFolders;
    j["scannerVersion"] = d.scannerVersion;
    j["musicAssistant"] = {{"enabled", d.maEnabled}, {"host", d.maHost}, {"port", d.maPort},
        {"username", d.maUsername}, {"password", d.maPassword}, {"token", d.maToken},
        {"playerId", d.maPlayerId}, {"playerName", d.maPlayerName}};
    Json sources = Json::array();
    for (const ArtSource& s : d.artSources)
        sources.push_back({{"id", s.id}, {"enabled", s.enabled}, {"apiKey", s.apiKey}});
    j["artwork"] = {{"fetchOnline", d.fetchOnlineArt}, {"sources", sources}};
    j["ui"] = {{"volume", d.volume}, {"viewMode", d.viewMode}, {"playlistViewMode", d.playlistViewMode}, {"selectedSource", d.selectedSource},
        {"shuffle", d.shuffle}, {"repeat", d.repeat}, {"windowX", d.windowX}, {"windowY", d.windowY},
        {"windowW", d.windowW}, {"windowH", d.windowH}, {"sidebarWidth", d.sidebarWidth}};
    std::string tmp = fPath + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out)
            return false;
        out << j.dump(2) << "\n";
    }
    return std::rename(tmp.c_str(), fPath.c_str()) == 0;
}

SettingsData Settings::Get() const
{
    std::lock_guard<std::mutex> lock(fMutex);
    return fData;
}

void Settings::Update(const SettingsData& data)
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fData = data;
    }
    Save();
}

std::string GenerateClientId()
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::mt19937_64 rng((uint64_t)std::chrono::steady_clock::now().time_since_epoch().count()
        ^ (uint64_t)std::random_device{}());
    std::string id = "amp-";
    for (int i = 0; i < 16; i++)
        id.push_back(alphabet[rng() % (sizeof(alphabet) - 1)]);
    return id;
}

} // namespace amp
