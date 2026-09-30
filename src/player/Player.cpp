#include "Player.h"
#include "core/Library.h"
#include "core/MusicAssistant.h"
#include <Message.h>
#include <OS.h>
#include <cstdio>

namespace amp {

Player::Player(Library& library, MusicAssistant& ma)
    : fLibrary(library), fMA(ma), fDecoder(fOutput)
{
    fDecoder.onFinished = [this] { TrackFinished(fGeneration); };
}

Player::~Player()
{
    Shutdown();
}

void Player::Start()
{
    if (fRunning)
        return;
    fRunning = true;
    fTicker = std::thread([this] { Ticker(); });
}

void Player::Shutdown()
{
    fRunning = false;
    if (fTicker.joinable())
        fTicker.join();
    StopEngines(false);
    DisableMusicAssistant();
    fOutput.Close();
}

std::string Player::MAStatus()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fMAStatus;
}

void Player::EnableMusicAssistant(const std::string& host, int port, const std::string& token,
    const std::string& clientId, const std::string& playerName)
{
    DisableMusicAssistant();
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    fMAPlayerId = clientId;
    fMAQueueId.clear();
    fSendspinAudio.reset(new SendspinAudio(fOutput));
    fSendspinAudio->onStreamStarted = [this] {
        fMAStreamSeen = true;
        fMAStreamStartedAt = system_time();
        fMALastPoll = system_time() - 2000000; // first queue poll one second from now
        if (fCurrentIsMA && (fState == kLoading || fState == kPaused)) {
            fOutput.SetPlaying(true);
            fMAPaused = false;
            SetState(kPlaying);
        }
    };
    fSendspinAudio->onStreamEnded = [this] {
        // A stream/end right after stream/start belongs to the previous queue item being
        // replaced, not to the track that just started.
        if (fCurrentIsMA && fMAStreamSeen && fState != kStopped && !fMAPaused
            && system_time() - fMAStreamStartedAt > 1500000) {
            fMAStreamSeen = false;
            TrackFinished(fGeneration);
        }
    };
    fSendspinAudio->onVolume = [this](int volume) {
        fOutput.SetVolume(volume / 100.0f);
        BMessage message(kMsgPlayerVolume);
        message.AddFloat("volume", fOutput.Volume());
        fTarget.SendMessage(&message);
    };
    fSendspinAudio->onMute = [this](bool muted) { fOutput.SetMuted(muted); };
    fSendspinAudio->onConnection = [this](bool connected, const std::string& text) {
        {
            std::lock_guard<std::recursive_mutex> lock(fMutex);
            fMAStatus = text;
        }
        BMessage message(kMsgMAStatus);
        message.AddBool("connected", connected);
        message.AddString("message", text.c_str());
        fTarget.SendMessage(&message);
    };
    fSendspinAudio->Start();
    fSendspin.reset(new SendspinClient(fSendspinAudio.get()));
    fSendspin->Configure(host, port, token, clientId, playerName);
    fSendspin->ReportVolume((int)(fOutput.Volume() * 100), fOutput.Muted());
    fSendspin->Start();
}

void Player::DisableMusicAssistant()
{
    std::unique_ptr<SendspinClient> client;
    std::unique_ptr<SendspinAudio> audio;
    {
        std::lock_guard<std::recursive_mutex> lock(fMutex);
        client = std::move(fSendspin);
        audio = std::move(fSendspinAudio);
    }
    if (client)
        client->Stop();
    if (audio)
        audio->Stop();
}

void Player::StopMusicAssistantTrack()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fCurrentIsMA && fCurrentTrack != 0)
        Stop();
}

void Player::DropMissingTracks()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    bool currentGone = false;
    {
        Library::Locker locker(fLibrary);
        int64_t current = fCurrentTrack;
        currentGone = current != 0 && fLibrary.TrackById(current) == nullptr;
        fQueue.RemoveIf([this](int64_t id) { return fLibrary.TrackById(id) == nullptr; });
    }
    if (currentGone)
        Stop();
}

void Player::SetState(PlayerState state)
{
    if (fState == state)
        return;
    fState = state;
    PostState();
}

void Player::PostState()
{
    BMessage message(kMsgPlayerStateChanged);
    message.AddInt32("state", fState);
    message.AddInt64("track", fCurrentTrack);
    fTarget.SendMessage(&message);
}

void Player::PostError(const std::string& error)
{
    BMessage message(kMsgPlayerError);
    message.AddString("error", error.c_str());
    fTarget.SendMessage(&message);
}

void Player::PlayTracks(const std::vector<int64_t>& trackIds, int startIndex)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    fQueue.Set(trackIds, startIndex);
    StartCurrent();
}

void Player::EnqueueNext(const std::vector<int64_t>& trackIds)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    bool wasEmpty = fQueue.Empty();
    fQueue.InsertNext(trackIds);
    if (wasEmpty)
        StartCurrent();
}

void Player::EnqueueLast(const std::vector<int64_t>& trackIds)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    bool wasEmpty = fQueue.Empty();
    fQueue.Append(trackIds);
    if (wasEmpty)
        StartCurrent();
}

std::vector<int64_t> Player::QueueTracks()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fQueue.Tracks();
}

void Player::StopEngines(bool notify)
{
    fGeneration++;
    fDecoder.Stop();
    fOutput.SetPlaying(false);
    fOutput.AbortWriters();
    if (fCurrentIsMA && fSendspin && fSendspin->IsConnected() && notify) {
        std::string playerId = MAQueueId();
        std::thread([this, playerId] {
            std::string error;
            fMA.PlayerCommand("stop", playerId, Json::object(), error);
        }).detach();
    }
    fMAStreamSeen = false;
    fMAPaused = false;
}

void Player::StartCurrent()
{
    int64_t trackId = fQueue.Current();
    if (trackId == 0) {
        StopEngines(true);
        fCurrentTrack = 0;
        fCurrentIsMA = false;
        SetState(kStopped);
        return;
    }
    std::string path;
    bool isMA = false;
    int64_t duration = 0;
    {
        Library::Locker locker(fLibrary);
        const Track* track = fLibrary.TrackById(trackId);
        if (!track) {
            fQueue.RemoveTrack(trackId);
            StartCurrent();
            return;
        }
        path = track->uri;
        isMA = track->isMA();
        duration = track->durationMs;
    }
    StopEngines(!isMA);
    fCurrentTrack = trackId;
    fCurrentIsMA = isMA;
    fDurationMs = duration;
    PostState();
    if (isMA)
        StartMA(trackId, path);
    else
        StartLocal(trackId, path);
}

void Player::StartLocal(int64_t trackId, const std::string& path)
{
    std::string error;
    if (!fDecoder.Open(path, error)) {
        PostError("Cannot play \"" + path + "\": " + error);
        // skip to the next track
        if (fQueue.Next(false))
            StartCurrent();
        else
            SetState(kStopped);
        return;
    }
    if (fDecoder.DurationMs() > 0)
        fDurationMs = fDecoder.DurationMs();
    fOutput.SetPlaying(true);
    fDecoder.Start();
    fState = kPlaying;
    PostState();
}

void Player::StartMA(int64_t trackId, const std::string& uri)
{
    if (!fSendspin) {
        PostError("Music Assistant is not connected; enable it in Settings to play this track.");
        SetState(kStopped);
        return;
    }
    fMAStreamSeen = false;
    fMAPaused = false;
    fMAQuality = 0;
    fMAElapsedMs = 0;
    fMAElapsedAt = system_time();
    fMALastPoll = 0;
    fState = kLoading;
    PostState();
    int generation = fGeneration;
    std::thread([this, generation, uri] { MAWorker(generation, uri); }).detach();
}

std::string Player::MAQueueId()
{
    {
        std::lock_guard<std::recursive_mutex> lock(fMutex);
        if (!fMAQueueId.empty())
            return fMAQueueId;
    }
    std::string error;
    std::string resolved = fMA.ResolvePlayerId(fMAPlayerId, error);
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fMAQueueId.empty())
        fMAQueueId = resolved;
    return fMAQueueId;
}

void Player::MAWorker(int generation, std::string uri)
{
    // wait briefly for the Sendspin registration when it is still connecting
    for (int i = 0; i < 50 && fRunning && generation == fGeneration; i++) {
        if (fSendspin && fSendspin->IsConnected())
            break;
        snooze(100000);
    }
    if (generation != fGeneration)
        return;
    if (!fSendspin || !fSendspin->IsConnected()) {
        PostError("Music Assistant player is not connected (" + MAStatus() + ")");
        SetState(kStopped);
        return;
    }
    std::string error;
    std::string playerId = MAQueueId();
    if (!fMA.PlayMedia(playerId, uri, error)) {
        if (generation != fGeneration)
            return;
        {
            // the wrapper player may have been re-created; resolve again next time
            std::lock_guard<std::recursive_mutex> lock(fMutex);
            fMAQueueId.clear();
        }
        std::string reason = error.find("HTTP 500") != std::string::npos
            ? "the server reported an error (is the track's provider online?)" : error;
        PostError("Music Assistant cannot play \"" + uri + "\": " + reason);
        std::lock_guard<std::recursive_mutex> lock(fMutex);
        if (generation != fGeneration)
            return;
        StopEngines(false);
        fCurrentTrack = 0;
        SetState(kStopped);
        return;
    }
    fOutput.SetPlaying(true);
}

void Player::TrackFinished(int generation)
{
    // Runs on a decoder or network thread: hand over to the application looper so that
    // engines are never stopped (joined) from their own threads.
    if (generation != fGeneration)
        return;
    BMessage message(kMsgTrackFinished);
    message.AddInt32("generation", generation);
    if (fControl.IsValid())
        fControl.SendMessage(&message);
    else
        HandleTrackFinished(generation);
}

void Player::HandleTrackFinished(int generation)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (generation != fGeneration)
        return;
    if (fQueue.Next(false))
        StartCurrent();
    else {
        StopEngines(false);
        fCurrentTrack = 0;
        SetState(kStopped);
    }
}

void Player::PlayPause()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    switch (fState) {
        case kPlaying:
            fOutput.SetPlaying(false);
            if (fCurrentIsMA && fSendspin) {
                fMAPaused = true;
                std::string playerId = MAQueueId();
                std::thread([this, playerId] {
                    std::string error;
                    fMA.PlayerCommand("pause", playerId, Json::object(), error);
                }).detach();
            }
            SetState(kPaused);
            break;
        case kPaused:
            if (fCurrentIsMA && fSendspin) {
                std::string playerId = MAQueueId();
                std::thread([this, playerId] {
                    std::string error;
                    fMA.PlayerCommand("play", playerId, Json::object(), error);
                }).detach();
                fMAPaused = false;
            }
            fOutput.SetPlaying(true);
            SetState(kPlaying);
            break;
        case kStopped:
            if (!fQueue.Empty())
                StartCurrent();
            break;
        default:
            break;
    }
}

void Player::Stop()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    StopEngines(true);
    fCurrentTrack = 0;
    SetState(kStopped);
}

void Player::Next()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fQueue.Empty())
        return;
    if (fQueue.Next(true))
        StartCurrent();
    else
        Stop();
}

void Player::Previous()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fQueue.Empty())
        return;
    if (PositionMs() > 3000) {
        Seek(0);
        return;
    }
    fQueue.Previous();
    StartCurrent();
}

void Player::Seek(int64_t positionMs)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fState == kStopped)
        return;
    if (fCurrentIsMA) {
        if (!fSendspin)
            return;
        fMAElapsedMs = positionMs;
        fMAElapsedAt = system_time();
        std::string playerId = MAQueueId();
        std::thread([this, playerId, positionMs] {
            std::string error;
            fMA.PlayerCommand("seek", playerId, Json{{"position", (int)(positionMs / 1000)}}, error);
        }).detach();
    } else
        fDecoder.Seek(positionMs);
}

void Player::SetVolume(float volume)
{
    fOutput.SetVolume(volume);
    if (fSendspin)
        fSendspin->ReportVolume((int)(volume * 100), fOutput.Muted());
}

void Player::SetShuffle(bool shuffle)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    fQueue.SetShuffle(shuffle);
}

bool Player::Shuffle()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fQueue.Shuffle();
}

void Player::SetRepeat(RepeatMode mode)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    fQueue.SetRepeat(mode);
}

RepeatMode Player::Repeat()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fQueue.Repeat();
}

int64_t Player::PositionMs()
{
    if (fState == kStopped)
        return 0;
    if (fCurrentIsMA) {
        int64_t position = fMAElapsedMs;
        if (fState == kPlaying)
            position += (system_time() - fMAElapsedAt) / 1000;
        if (fDurationMs > 0 && position > fDurationMs)
            position = fDurationMs;
        return position;
    }
    return fDecoder.PositionMs();
}

void Player::Ticker()
{
    while (fRunning) {
        snooze(500000);
        if (!fRunning)
            break;
        if (fState == kStopped)
            continue;
        if (fCurrentIsMA && fState == kPlaying && system_time() - fMALastPoll > 3000000) {
            fMALastPoll = system_time();
            std::string playerId = MAQueueId();
            int generation = fGeneration;
            std::thread([this, playerId, generation] {
                MAQueueState state = fMA.QueueState(playerId);
                if (generation != fGeneration || !state.valid)
                    return;
                if (state.state == "playing" || state.state == "paused") {
                    fMAElapsedMs = (int64_t)(state.elapsed * 1000);
                    fMAElapsedAt = system_time();
                    if (state.durationMs > 0)
                        fDurationMs = state.durationMs;
                    if (state.qualityKnown)
                        fMAQuality = state.lossless ? -1 : state.bitrate;
                }
            }).detach();
        }
        BMessage message(kMsgPlayerProgress);
        message.AddInt64("position", PositionMs());
        message.AddInt64("duration", fDurationMs);
        message.AddInt64("track", fCurrentTrack);
        message.AddInt32("quality", StreamQuality());
        fTarget.SendMessage(&message);
    }
}

} // namespace amp
