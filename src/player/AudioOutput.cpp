#include "AudioOutput.h"
#include <cstring>
#include <cmath>

namespace amp {

AudioOutput::AudioOutput() {}

AudioOutput::~AudioOutput()
{
    Close();
}

bool AudioOutput::Open(int sampleRate)
{
    if (fPlayer && fSampleRate == sampleRate)
        return true;
    Close();
    media_raw_audio_format format = media_raw_audio_format::wildcard;
    format.frame_rate = (float)sampleRate;
    format.channel_count = 2;
    format.format = media_raw_audio_format::B_AUDIO_FLOAT;
    format.byte_order = B_MEDIA_HOST_ENDIAN;
    format.buffer_size = 2048 * sizeof(float) * 2; // 2048 frames per buffer
    fPlayer = new BSoundPlayer(&format, "Amp", PlayBuffer, nullptr, this);
    if (fPlayer->InitCheck() != B_OK) {
        delete fPlayer;
        fPlayer = nullptr;
        return false;
    }
    fSampleRate = sampleRate;
    fCapacity = (size_t)sampleRate * 3 / 2; // 1.5 seconds
    fRing.assign(fCapacity * 2, 0);
    fReadPos = fWritePos = 0;
    fCount = 0;
    fAbort = false;
    fConsumed = 0;
    fPlayer->SetHasData(true);
    fPlayer->Start();
    return true;
}

void AudioOutput::Close()
{
    AbortWriters();
    if (fPlayer) {
        fPlayer->Stop();
        delete fPlayer;
        fPlayer = nullptr;
    }
    fSampleRate = 0;
    fCount = 0;
    fReadPos = fWritePos = 0;
}

void AudioOutput::SetVolume(float volume)
{
    if (volume < 0)
        volume = 0;
    if (volume > 1)
        volume = 1;
    fVolume = volume;
}

void AudioOutput::SetPlaying(bool playing)
{
    fPlaying = playing;
}

bigtime_t AudioOutput::Latency() const
{
    return fPlayer ? fPlayer->Latency() : 0;
}

bool AudioOutput::Write(const int16_t* frames, size_t frameCount)
{
    size_t written = 0;
    while (written < frameCount) {
        std::unique_lock<std::mutex> lock(fMutex);
        fSpace.wait_for(lock, std::chrono::milliseconds(200), [this] { return fAbort || fCount < fCapacity; });
        if (fAbort)
            return false;
        if (fCount >= fCapacity)
            continue;
        size_t free = fCapacity - fCount;
        size_t chunk = std::min(free, frameCount - written);
        size_t untilEnd = fCapacity - fWritePos;
        size_t first = std::min(chunk, untilEnd);
        memcpy(&fRing[fWritePos * 2], frames + written * 2, first * 2 * sizeof(int16_t));
        if (chunk > first)
            memcpy(&fRing[0], frames + (written + first) * 2, (chunk - first) * 2 * sizeof(int16_t));
        fWritePos = (fWritePos + chunk) % fCapacity;
        fCount += chunk;
        written += chunk;
    }
    return true;
}

void AudioOutput::Flush()
{
    std::lock_guard<std::mutex> lock(fMutex);
    fReadPos = fWritePos = 0;
    fCount = 0;
    fSpace.notify_all();
}

void AudioOutput::AbortWriters()
{
    fAbort = true;
    fSpace.notify_all();
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fReadPos = fWritePos = 0;
        fCount = 0;
    }
    fAbort = false;
}

size_t AudioOutput::BufferedFrames()
{
    return fCount;
}

void AudioOutput::PlayBuffer(void* cookie, void* buffer, size_t size, const media_raw_audio_format& format)
{
    AudioOutput* self = static_cast<AudioOutput*>(cookie);
    size_t frames = size / (sizeof(float) * format.channel_count);
    self->Fill(static_cast<float*>(buffer), frames);
}

void AudioOutput::Fill(float* out, size_t frames)
{
    if (!fPlaying) {
        memset(out, 0, frames * 2 * sizeof(float));
        return;
    }
    float target = fMuted ? 0.0f : fVolume;
    // perceptual curve: slider 0..1 -> amplitude
    target = target * target;
    size_t available = fCount;
    size_t take = std::min(frames, available);
    if (take < frames)
        fUnderrun = true;
    else
        fUnderrun = false;
    float gain = fCurrentGain;
    float step = (target - gain) / (float)std::max<size_t>(frames, 1);
    size_t readPos = fReadPos;
    for (size_t i = 0; i < take; i++) {
        gain += step;
        out[i * 2] = (float)fRing[readPos * 2] * (gain / 32768.0f);
        out[i * 2 + 1] = (float)fRing[readPos * 2 + 1] * (gain / 32768.0f);
        readPos++;
        if (readPos == fCapacity)
            readPos = 0;
    }
    fCurrentGain = target;
    if (take < frames)
        memset(out + take * 2, 0, (frames - take) * 2 * sizeof(float));
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fReadPos = readPos;
        fCount -= take;
    }
    fConsumed += (int64_t)take;
    fSpace.notify_all();
}

} // namespace amp
