// Where an artist lives on MusicBrainz: the artist page when the artist's MBID is known, a
// search for the name otherwise. MBIDs come from the lookups the artwork code makes anyway
// (MusicBrainz releases, TheAudioDB artists) and from a lookup by name when an artist is
// opened; they are kept in a small file in the cache directory.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace amp {

class ArtistLinks {
public:
    // The one store of the process.
    static ArtistLinks& Shared();
    // Loads what earlier runs learned and keeps it in `directory`/artist-mbids.json from now on.
    void Open(const std::string& directory);

    // Notes the MBID of an artist seen in a MusicBrainz or TheAudioDB answer.
    void Remember(const std::string& artist, const std::string& mbid);
    // The MBID known for the artist, or empty.
    std::string Known(const std::string& artist) const;
    // Known(), or else asks MusicBrainz for the one artist of exactly that name; `album`, one of
    // the artist's albums, settles a name several artists share. Blocks for a second or two
    // (MusicBrainz takes one request per second); empty when no single artist was found.
    // A name that found nothing is not asked about again for a week.
    std::string Resolve(const std::string& artist, const std::string& album);

    // The page to open for the artist: its MusicBrainz page with an MBID, a search without.
    static std::string PageUrl(const std::string& artist, const std::string& mbid);
    static std::string ArtistUrl(const std::string& mbid);
    static std::string SearchUrl(const std::string& artist);
    // The artist of exactly that name in a MusicBrainz artist search reply (JSON), empty when
    // there is none or several.
    static std::string PickArtist(const std::string& artist, const std::string& reply);
    // The credited artist of that name in a MusicBrainz release search reply, empty if none.
    static std::string PickCreditedArtist(const std::string& artist, const std::string& reply);
    static bool IsMbid(const std::string& text);

private:
    struct Entry {
        std::string mbid;       // empty: looked up, nothing found
        int64_t checked = 0;    // when (seconds since the epoch)
    };
    void Store(const std::string& artist, const Entry& entry);
    void SaveLocked() const;

    mutable std::mutex fMutex;
    std::map<std::string, Entry> fEntries;   // by squashed name
    std::string fPath;
};

} // namespace amp
