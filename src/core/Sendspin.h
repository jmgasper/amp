// Sendspin "player@v1" client speaking the unencrypted transition-mode protocol that
// Music Assistant 2.10 accepts through its authenticated /sendspin WebSocket proxy.
// Amp registers itself as a player so the server streams PCM audio to it.
#pragma once
#include "TimeFilter.h"
#include "WebSocket.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace amp {

struct SendspinFormat {
    std::string codec = "pcm";
    int sampleRate = 44100;
    int channels = 2;
    int bitDepth = 16;
};

class SendspinSink {
public:
    virtual ~SendspinSink() {}
    virtual void SendspinStreamStarted(const SendspinFormat& format) = 0;
    // pcm: little-endian interleaved samples in the stream format; playAtLocalUs: local clock time
    virtual void SendspinChunk(int64_t playAtLocalUs, const char* pcm, size_t size) = 0;
    virtual void SendspinStreamCleared() = 0;
    virtual void SendspinStreamEnded() = 0;
    virtual void SendspinVolume(int volume) = 0;
    virtual void SendspinMute(bool muted) = 0;
    virtual void SendspinGroupState(const std::string& playbackState) = 0;
    virtual void SendspinConnection(bool connected, const std::string& message) = 0;
};

class SendspinClient {
public:
    SendspinClient(SendspinSink* sink);
    ~SendspinClient();

    void Configure(const std::string& host, int port, const std::string& token,
        const std::string& clientId, const std::string& name);
    void Start();          // connects (and keeps reconnecting) in a background thread
    void Stop();           // disconnects and stops the thread
    bool IsConnected() const { return fConnected; }
    bool StreamActive() const { return fStreamActive; }
    std::string ClientId() const { return fClientId; }

    // Report state changes back to the server (volume in 0-100).
    void ReportVolume(int volume, bool muted);

    static int64_t NowUs();

private:
    void Run();
    bool Session();
    bool SendJson(const std::string& text);
    void SendClientTime();
    void SendClientState();
    void HandleText(const std::string& text);
    void HandleBinary(const std::string& data);

    SendspinSink* fSink;
    std::string fHost;
    int fPort = 8095;
    std::string fToken;
    std::string fClientId;
    std::string fName;
    std::unique_ptr<WebSocket> fSocket;
    std::thread fThread;
    std::atomic<bool> fRunning{false};
    std::atomic<bool> fConnected{false};
    std::atomic<bool> fStreamActive{false};
    std::atomic<bool> fStateDirty{false};
    TimeFilter fFilter;
    SendspinFormat fFormat;
    int fVolume = 100;
    bool fMuted = false;
    int64_t fLastTimeSync = 0;
    int fTimeSyncCount = 0;
    bool fAvailableReported = false;
    std::mutex fMutex;
};

} // namespace amp
