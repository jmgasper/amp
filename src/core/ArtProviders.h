// Online artwork lookups: Deezer, MusicBrainz + Cover Art Archive, TheAudioDB and Discogs.
#pragma once
#include "Model.h"
#include "Settings.h"
#include <string>
#include <vector>

namespace amp {

class ArtProviders {
public:
    // Returns candidate image URLs (best first) and the headers to send when downloading them.
    static std::vector<std::string> Lookup(const ArtSource& source, const ArtRequest& request,
        std::vector<std::string>& downloadHeaders);

    static std::vector<std::string> Deezer(const ArtRequest& request);
    static std::vector<std::string> MusicBrainz(const ArtRequest& request);
    static std::vector<std::string> TheAudioDB(const std::string& apiKey, const ArtRequest& request);
    static std::vector<std::string> Discogs(const std::string& token, const ArtRequest& request,
        std::vector<std::string>& downloadHeaders);
};

} // namespace amp
