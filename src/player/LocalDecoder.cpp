#include "LocalDecoder.h"
#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <cstring>
#include <vector>

namespace amp {

LocalDecoder::LocalDecoder(AudioOutput& output)
    : fOutput(output)
{
}

LocalDecoder::~LocalDecoder()
{
    Stop();
}

bool LocalDecoder::Open(const std::string& path, std::string& error)
{
    Stop();
    entry_ref ref;
    if (get_ref_for_path(path.c_str(), &ref) != B_OK) {
        error = "file not found";
        return false;
    }
    fFile = new BMediaFile(&ref);
    if (fFile->InitCheck() != B_OK) {
        error = "unsupported file format";
        CloseFile();
        return false;
    }
    for (int32 i = 0; i < fFile->CountTracks(); i++) {
        BMediaTrack* track = fFile->TrackAt(i);
        if (!track)
            continue;
        media_format encoded;
        if (track->EncodedFormat(&encoded) == B_OK && (encoded.IsAudio())) {
            fTrack = track;
            break;
        }
        fFile->ReleaseTrack(track);
    }
    if (!fTrack) {
        error = "no audio track";
        CloseFile();
        return false;
    }
    memset(&fFormat, 0, sizeof(fFormat));
    fFormat.type = B_MEDIA_RAW_AUDIO;
    fFormat.u.raw_audio = media_raw_audio_format::wildcard;
    fFormat.u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;
    fFormat.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
    if (fTrack->DecodedFormat(&fFormat) != B_OK) {
        error = "cannot decode audio";
        CloseFile();
        return false;
    }
    fRate = (int)fFormat.u.raw_audio.frame_rate;
    fChannels = (int)fFormat.u.raw_audio.channel_count;
    fFloat = fFormat.u.raw_audio.format == media_raw_audio_format::B_AUDIO_FLOAT;
    if (fRate <= 0)
        fRate = 44100;
    if (fChannels <= 0)
        fChannels = 2;
    fDurationMs = fTrack->Duration() / 1000;
    if (!fOutput.Open(fRate)) {
        error = "cannot open audio output";
        CloseFile();
        return false;
    }
    fFinished = false;
    fSeekTo = -1;
    fBaseMs = 0;
    fWrittenFrames = 0;
    fOutput.Flush();
    fOutput.ResetConsumed();
    fConsumedAtBase = 0;
    return true;
}

void LocalDecoder::CloseFile()
{
    if (fFile) {
        if (fTrack)
            fFile->ReleaseTrack(fTrack);
        delete fFile;
    }
    fFile = nullptr;
    fTrack = nullptr;
}

void LocalDecoder::Start()
{
    if (!fFile || fRunning)
        return;
    fRunning = true;
    fThread = std::thread([this] { Run(); });
}

void LocalDecoder::Stop()
{
    fRunning = false;
    fOutput.AbortWriters();
    if (fThread.joinable())
        fThread.join();
    CloseFile();
}

void LocalDecoder::Seek(int64_t positionMs)
{
    if (positionMs < 0)
        positionMs = 0;
    if (fDurationMs > 0 && positionMs > fDurationMs)
        positionMs = fDurationMs;
    fSeekTo = positionMs;
}

int64_t LocalDecoder::PositionMs()
{
    int64_t consumed = fOutput.FramesConsumed() - fConsumedAtBase;
    if (consumed < 0)
        consumed = 0;
    int64_t position = fBaseMs + consumed * 1000 / fRate;
    if (fDurationMs > 0 && position > fDurationMs)
        position = fDurationMs;
    return position;
}

void LocalDecoder::Run()
{
    size_t bufferSize = fFormat.u.raw_audio.buffer_size;
    if (bufferSize < 4096)
        bufferSize = 4096 * 8;
    std::vector<char> buffer(bufferSize + 4096);
    std::vector<int16_t> stereo;
    int64_t frameSize = (fFloat ? 4 : 2) * fChannels;
    bool ended = false;
    int errors = 0;
    bool pendingFrames = false;
    while (fRunning) {
        int64_t seek = fSeekTo.exchange(-1);
        if (seek >= 0) {
            bigtime_t time = seek * 1000;
            fOutput.AbortWriters();
            if (fTrack->SeekToTime(&time) == B_OK) {
                fBaseMs = time / 1000;
            } else
                fBaseMs = seek;
            fOutput.Flush();
            fConsumedAtBase = fOutput.FramesConsumed();
            ended = false;
        }
        if (ended && !pendingFrames) {
            // wait for the buffer to drain, then report completion
            if (fOutput.BufferedFrames() == 0 || !fOutput.Playing()) {
                if (fOutput.BufferedFrames() == 0) {
                    fFinished = true;
                    if (onFinished)
                        onFinished();
                    break;
                }
            }
            snooze(20000);
            continue;
        }
        int64 frameCount = 0;
        media_header header;
        status_t status = fTrack->ReadFrames(buffer.data(), &frameCount, &header);
        if (status != B_OK) {
            if (status == B_LAST_BUFFER_ERROR || ++errors > 8) {
                if (frameCount <= 0) {
                    ended = true;
                    continue;
                }
                ended = true; // deliver the final frames below, then drain
                pendingFrames = true;
            } else if (frameCount <= 0) {
                continue; // transient decoder error: try the next buffer
            }
        } else
            errors = 0;
        if (frameCount <= 0)
            continue;
        stereo.resize((size_t)frameCount * 2);
        if (fFloat) {
            const float* in = reinterpret_cast<const float*>(buffer.data());
            for (int64 i = 0; i < frameCount; i++) {
                float l = in[i * fChannels];
                float r = fChannels > 1 ? in[i * fChannels + 1] : l;
                stereo[i * 2] = (int16_t)std::max(-32768.0f, std::min(32767.0f, l * 32767.0f));
                stereo[i * 2 + 1] = (int16_t)std::max(-32768.0f, std::min(32767.0f, r * 32767.0f));
            }
        } else {
            const int16_t* in = reinterpret_cast<const int16_t*>(buffer.data());
            for (int64 i = 0; i < frameCount; i++) {
                stereo[i * 2] = in[i * fChannels];
                stereo[i * 2 + 1] = fChannels > 1 ? in[i * fChannels + 1] : in[i * fChannels];
            }
        }
        (void)frameSize;
        pendingFrames = false;
        if (!fOutput.Write(stereo.data(), (size_t)frameCount))
            continue; // aborted (seek/stop); loop re-checks state
        fWrittenFrames += frameCount;
    }
}

} // namespace amp
