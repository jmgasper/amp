// Playback controller: owns the audio engines, the play queue and the Music Assistant player link.
#pragma once
#include "AudioOutput.h"
#include "LocalDecoder.h"
#include "Messages.h"
#include "SendspinAudio.h"
#include "core/Queue.h"
#include "core/Sendspin.h"
#include <Messenger.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tasamp {

class Library;
class MusicAssistant;

class Player {
public:
    Player(Library& library, MusicAssistant& ma);
    ~Player();

    void SetTarget(const BMessenger& target) { fTarget = target; }
    void SetControl(const BMessenger& control) { fControl = control; }
    // Called by the application looper when a kMsgTrackFinished message arrives.
    void HandleTrackFinished(int generation);
    void Start();
    void Shutdown();

    // Music Assistant player registration (Sendspin). Safe to call again with new settings.
    void EnableMusicAssistant(const std::string& host, int port, const std::string& token,
        const std::string& clientId, const std::string& playerName);
    void DisableMusicAssistant();
    bool MAConnected() const { return fSendspinAudio && fSendspinAudio->Connected(); }
    std::string MAStatus();

    void PlayTracks(const std::vector<int64_t>& trackIds, int startIndex);
    void PlayPause();
    void Stop();
    void Next();
    void Previous();
    void Seek(int64_t positionMs);
    void SetVolume(float volume);
    float Volume() const { return fOutput.Volume(); }
    void SetShuffle(bool shuffle);
    bool Shuffle();
    void SetRepeat(RepeatMode mode);
    RepeatMode Repeat();
    void EnqueueNext(const std::vector<int64_t>& trackIds);
    void EnqueueLast(const std::vector<int64_t>& trackIds);

    PlayerState State() const { return fState; }
    int64_t CurrentTrack() const { return fCurrentTrack; }
    int64_t PositionMs();
    int64_t DurationMs() const { return fDurationMs; }
    // Quality reported by Music Assistant for the running stream: 0 unknown, -1 lossless, else kbit/s.
    int StreamQuality() const { return fCurrentIsMA ? fMAQuality.load() : 0; }
    std::vector<int64_t> QueueTracks();

private:
    void StartCurrent();
    void StartLocal(int64_t trackId, const std::string& path);
    void StartMA(int64_t trackId, const std::string& uri);
    void StopEngines(bool notify);
    void TrackFinished(int generation);
    void SetState(PlayerState state);
    void PostState();
    void PostError(const std::string& error);
    void Ticker();
    void MAWorker(int generation, std::string uri);
    std::string MAQueueId();

    Library& fLibrary;
    MusicAssistant& fMA;
    BMessenger fTarget;
    BMessenger fControl;
    AudioOutput fOutput;
    LocalDecoder fDecoder;
    std::unique_ptr<SendspinAudio> fSendspinAudio;
    std::unique_ptr<SendspinClient> fSendspin;
    std::string fMAPlayerId;      // our Sendspin client id
    std::string fMAQueueId;       // the Music Assistant player/queue that streams to us
    std::string fMAStatus;
    PlayQueue fQueue;
    std::recursive_mutex fMutex;
    std::atomic<PlayerState> fState{kStopped};
    std::atomic<int64_t> fCurrentTrack{0};
    std::atomic<int64_t> fDurationMs{0};
    std::atomic<bool> fCurrentIsMA{false};
    std::atomic<int> fGeneration{0};
    std::atomic<bool> fMAStreamSeen{false};
    std::atomic<int64_t> fMAStreamStartedAt{0};
    std::atomic<bool> fRunning{false};
    std::thread fTicker;
    // MA position tracking
    std::atomic<int64_t> fMAElapsedMs{0};
    std::atomic<int64_t> fMAElapsedAt{0};     // system_time when fMAElapsedMs was sampled
    std::atomic<int64_t> fMALastPoll{0};
    std::atomic<bool> fMAPaused{false};
    std::atomic<int> fMAQuality{0};          // 0 unknown, -1 lossless, >0 kbit/s (current MA stream)
};

} // namespace tasamp
