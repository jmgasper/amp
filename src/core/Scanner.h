// Background scanner: walks the configured folders, reads tags with TagLib and feeds the library.
#pragma once
#include "Model.h"
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace tasamp {

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

    // status text, done flag. Called from the scanner thread.
    std::function<void(const std::string&, bool)> onProgress;
    // Optional probe returning the duration in ms of a file whose tags carry none (raw AAC etc.).
    std::function<int64_t(const std::string&)> durationProbe;

    // Reads tags of one file. Returns false if the file is not a supported audio file.
    static bool ReadTrack(const std::string& path, Track& track, std::string* embeddedArt, std::string* embeddedMime);
    static bool IsAudioFile(const std::string& path);

private:
    void Run(std::vector<std::string> folders, bool force);
    void Walk(const std::string& folder, std::vector<std::string>& files);

    Library& fLibrary;
    ImageCache& fImages;
    std::thread fThread;
    std::atomic<bool> fRunning{false};
    std::atomic<bool> fStop{false};
};

} // namespace tasamp
