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

    // Waits until MusicBrainz may be asked again: it takes one request per second per client.
    static void MusicBrainzThrottle();
    // A name for loose comparison: lower-case letters and digits only.
    static std::string SquashName(const std::string& text);
};

} // namespace amp
