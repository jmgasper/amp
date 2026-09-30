// Background scanner: walks the configured folders, reads tags with TagLib and feeds the library.
// Folders are listed and files are read by several workers at once; one scan of a big
// library on a network share is mostly waiting for the server.
#pragma once
#include "Model.h"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace amp {

class Library;
class ImageCache;

class Scanner {
public:
    Scanner(Library& library, ImageCache& images);
    ~Scanner();

    // force: read every file again even when size and mtime are unchanged
    void Start(const std::vector<std::string>& folders, bool force = false);
    void Stop();
    bool IsRunning() const { return fRunning; }
    // How many files are read side by side; 0 picks a number that suits the machine.
    void SetWorkerCount(int count) { fWorkerCount = count; }

    // status text, done flag. Called from the scanner thread.
    std::function<void(const std::string&, bool)> onProgress;
    // Optional probe returning the duration in ms of a file whose tags carry none (raw AAC etc.).
    // Called from the workers, one call at a time.
    std::function<int64_t(const std::string&)> durationProbe;

    // Reads tags of one file. Returns false if the file is not a supported audio file.
    static bool ReadTrack(const std::string& path, Track& track, std::string* embeddedArt, std::string* embeddedMime);
    // The same for a file whose size and modification time are known. `wantArt` is asked,
    // once the tags are read, whether the embedded picture is worth copying.
    static bool ReadTrack(const std::string& path, int64_t size, int64_t modified, Track& track,
        std::string* embeddedArt, std::string* embeddedMime, const std::function<bool(const Track&)>& wantArt = nullptr);
    static bool IsAudioFile(const std::string& path);

private:
    struct FoundFile {
        std::string path;
        int64_t modified = 0;
        int64_t size = 0;
    };
    // What the workers of one scan share.
    struct Pass {
        std::vector<FoundFile> files;                  // every audio file below the folders
        std::map<std::string, std::string> folderArt;  // folder -> its cover picture
        std::vector<std::string> unreadable;           // folders that could not be listed
        std::vector<const FoundFile*> changed;         // the files to read
        std::atomic<size_t> next{0};
        std::atomic<int> processed{0};
        std::atomic<int> added{0};
        std::mutex mutex;
        std::vector<Track> pending;                    // read, not yet in the library
        std::set<ArtKey> artClaimed;                   // albums whose artwork a worker is storing
        std::mutex probeMutex;
        int64_t lastRebuild = 0;
        int64_t lastProgress = 0;
    };
    void Run(std::vector<std::string> folders, bool force);
    void Walk(const std::vector<std::string>& folders, int workers, Pass& pass);
    void ReadFile(const FoundFile& file, Pass& pass);
    void Flush(Pass& pass, bool everything);
    void ReportProgress(Pass& pass);
    int WorkerCount() const;

    Library& fLibrary;
    ImageCache& fImages;
    std::thread fThread;
    std::atomic<bool> fRunning{false};
    std::atomic<bool> fStop{false};
    int fWorkerCount = 0;
};

} // namespace amp
