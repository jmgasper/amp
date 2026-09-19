// Disk cache for artwork with a background fetcher (Music Assistant image proxy, folder art,
// online providers). Images are stored as downloaded; scaling happens in the UI layer.
#pragma once
#include "Model.h"
#include "Settings.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace tasamp {

class ImageCache {
public:
    ImageCache(const std::string& cacheDir, Settings& settings);
    ~ImageCache();

    bool Open();
    void Start();
    void Stop();

    // Path of a cached image or "" (fast, in-memory index).
    std::string PathFor(const ArtKey& key);
    bool Has(const ArtKey& key);
    // True when a lookup already failed recently; no point in requesting again.
    bool KnownMissing(const ArtKey& key);
    bool Store(const ArtKey& key, const std::string& bytes, const std::string& mime);
    // Queue a fetch. Front requests (items visible right now) are served first; asking again
    // for a key that is already queued moves it to the front. Direct downloads (Music
    // Assistant image proxy, remote URLs) run on their own workers so that slow online
    // lookups never hold them up.
    void Request(const ArtRequest& request, bool front = true);
    void ClearAll();
    int64_t CacheSizeBytes();

    // Called from the worker thread when an image became available (or definitely failed: path "").
    std::function<void(const ArtKey&, const std::string& path)> onReady;

private:
    typedef std::deque<ArtRequest> Queue;
    void RunDirect();
    void RunOnline();
    bool NextRequest(Queue& queue, ArtRequest& request);
    void Enqueue(Queue& queue, const ArtRequest& request, bool front);
    void Finish(const ArtRequest& request, bool ok);
    bool OnlineLookupPossible(const ArtRequest& request);
    bool FetchOnline(const ArtRequest& request);
    bool StoreDownloaded(const ArtKey& key, const std::string& url, const std::vector<std::string>& headers);
    void MarkMissing(const ArtKey& key);
    bool MissingLocked(const ArtKey& key);
    std::string FileNameFor(const ArtKey& key, const std::string& mime);

    std::string fDir;
    Settings& fSettings;
    sqlite3* fDb = nullptr;
    std::mutex fMutex;
    std::unordered_map<ArtKey, std::string> fIndex;    // key -> file path
    std::unordered_map<ArtKey, int64_t> fMissing;      // key -> time of failed lookup
    Queue fDirectQueue;                                // requests with a URL
    Queue fOnlineQueue;                                // provider lookups
    std::set<ArtKey> fQueued;                          // keys waiting in either queue
    std::set<ArtKey> fActive;                          // keys being fetched right now
    std::condition_variable fCondition;
    std::vector<std::thread> fThreads;
    std::atomic<bool> fRunning{false};
};

} // namespace tasamp
