#include "Model.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace tasamp {

std::string ToLower(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text)
        out.push_back(c < 128 ? (char)std::tolower(c) : (char)c);
    return out;
}

bool ContainsNoCase(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

std::string SortKeyFor(const std::string& name)
{
    std::string key = ToLower(name);
    size_t start = 0;
    while (start < key.size() && std::isspace((unsigned char)key[start]))
        start++;
    key.erase(0, start);
    static const char* prefixes[] = {"the ", "a ", "an "};
    for (const char* prefix : prefixes) {
        size_t n = std::char_traits<char>::length(prefix);
        if (key.compare(0, n, prefix) == 0 && key.size() > n) {
            key.erase(0, n);
            break;
        }
    }
    return key;
}

std::string MakeAlbumArtKey(const std::string& artist, const std::string& album)
{
    std::string key = "album:" + ToLower(artist) + "|" + ToLower(album);
    for (char& c : key)
        if (c == '/' || c == '\\')
            c = '_';
    return key;
}

std::string MakeArtistArtKey(const std::string& artist)
{
    std::string key = "artist:" + ToLower(artist);
    for (char& c : key)
        if (c == '/' || c == '\\')
            c = '_';
    return key;
}

std::string QualityLabel(bool lossless, int bitrateKbps)
{
    if (lossless)
        return "Lossless";
    if (bitrateKbps > 0)
        return std::to_string(bitrateKbps) + " kbps";
    return "";
}

std::string FormatDuration(int64_t ms)
{
    if (ms <= 0)
        return "";
    int64_t seconds = ms / 1000;
    int64_t hours = seconds / 3600;
    int64_t minutes = (seconds % 3600) / 60;
    int64_t secs = seconds % 60;
    char buffer[32];
    if (hours > 0)
        snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", (long long)hours, (long long)minutes, (long long)secs);
    else
        snprintf(buffer, sizeof(buffer), "%lld:%02lld", (long long)minutes, (long long)secs);
    return buffer;
}

} // namespace tasamp
