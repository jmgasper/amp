// Decodes a local audio file with the Media Kit (BMediaFile/BMediaTrack) into the AudioOutput.
#pragma once
#include "AudioOutput.h"
#include <MediaDefs.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

class BMediaFile;
class BMediaTrack;

namespace tasamp {

class LocalDecoder {
public:
    explicit LocalDecoder(AudioOutput& output);
    ~LocalDecoder();

    bool Open(const std::string& path, std::string& error);
    void Start();
    void Stop();              // stops decoding and closes the file
    bool IsOpen() const { return fFile != nullptr; }
    void Seek(int64_t positionMs);
    int64_t DurationMs() const { return fDurationMs; }
    int64_t PositionMs();
    bool Finished() const { return fFinished; }

    std::function<void()> onFinished;   // called from the decoder thread when playback drained

private:
    void Run();
    void CloseFile();

    AudioOutput& fOutput;
    BMediaFile* fFile = nullptr;
    BMediaTrack* fTrack = nullptr;
    media_format fFormat;
    int64_t fDurationMs = 0;
    int fRate = 44100;
    int fChannels = 2;
    bool fFloat = false;
    std::thread fThread;
    std::atomic<bool> fRunning{false};
    std::atomic<bool> fFinished{false};
    std::atomic<int64_t> fSeekTo{-1};
    std::atomic<int64_t> fBaseMs{0};      // position at the last seek
    std::atomic<int64_t> fWrittenFrames{0};
    std::atomic<int64_t> fConsumedAtBase{0};
};

} // namespace tasamp
