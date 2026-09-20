// Feeds Sendspin PCM chunks into the AudioOutput at their scheduled local play time.
#pragma once
#include "AudioOutput.h"
#include "core/Sendspin.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace amp {

class SendspinAudio : public SendspinSink {
public:
    explicit SendspinAudio(AudioOutput& output);
    ~SendspinAudio();

    void Start();
    void Stop();

    // SendspinSink
    void SendspinStreamStarted(const SendspinFormat& format) override;
    void SendspinChunk(int64_t playAtLocalUs, const char* pcm, size_t size) override;
    void SendspinStreamCleared() override;
    void SendspinStreamEnded() override;
    void SendspinVolume(int volume) override;
    void SendspinMute(bool muted) override;
    void SendspinGroupState(const std::string& playbackState) override;
    void SendspinConnection(bool connected, const std::string& message) override;

    std::function<void()> onStreamStarted;
    std::function<void()> onStreamEnded;
    std::function<void(int)> onVolume;
    std::function<void(bool)> onMute;
    std::function<void(bool, const std::string&)> onConnection;
    std::function<void(const std::string&)> onGroupState;

    bool StreamActive() const { return fStreamActive; }
    bool Connected() const { return fConnected; }

private:
    struct Chunk {
        int64_t playAtUs;
        std::vector<int16_t> frames; // stereo interleaved
    };
    void Run();
    void Convert(const char* pcm, size_t size, std::vector<int16_t>& out);

    AudioOutput& fOutput;
    SendspinFormat fFormat;
    std::mutex fMutex;
    std::condition_variable fCondition;
    std::deque<Chunk> fQueue;
    int64_t fQueuedFrames = 0;
    std::thread fThread;
    std::atomic<bool> fRunning{false};
    std::atomic<bool> fStreamActive{false};
    std::atomic<bool> fConnected{false};
    std::atomic<bool> fFlush{false};
    int fGeneration = 0;
};

} // namespace amp
