#include "SendspinAudio.h"
#include <OS.h>
#include <cstdio>
#include <cstring>

namespace amp {

SendspinAudio::SendspinAudio(AudioOutput& output)
    : fOutput(output)
{
}

SendspinAudio::~SendspinAudio()
{
    Stop();
}

void SendspinAudio::Start()
{
    if (fRunning)
        return;
    fRunning = true;
    fThread = std::thread([this] { Run(); });
}

void SendspinAudio::Stop()
{
    fRunning = false;
    fCondition.notify_all();
    fOutput.AbortWriters();
    if (fThread.joinable())
        fThread.join();
}

void SendspinAudio::SendspinStreamStarted(const SendspinFormat& format)
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        bool formatChanged = format.sampleRate != fFormat.sampleRate || format.channels != fFormat.channels
            || format.bitDepth != fFormat.bitDepth;
        fFormat = format;
        if (formatChanged || !fStreamActive) {
            fGeneration++;
            fQueue.clear();
            fQueuedFrames = 0;
            fFlush = true;
        }
    }
    fStreamActive = true;
    fOutput.AbortWriters();
    fCondition.notify_all();
    if (onStreamStarted)
        onStreamStarted();
}

void SendspinAudio::Convert(const char* pcm, size_t size, std::vector<int16_t>& out)
{
    int channels = fFormat.channels > 0 ? fFormat.channels : 2;
    int bytesPerSample = fFormat.bitDepth / 8;
    if (bytesPerSample <= 0)
        bytesPerSample = 2;
    size_t frameBytes = (size_t)channels * bytesPerSample;
    size_t frames = size / frameBytes;
    out.resize(frames * 2);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(pcm);
    for (size_t i = 0; i < frames; i++) {
        int16_t samples[2] = {0, 0};
        for (int c = 0; c < 2; c++) {
            int source = c < channels ? c : 0;
            const unsigned char* s = p + i * frameBytes + source * bytesPerSample;
            int32_t value = 0;
            switch (bytesPerSample) {
                case 2:
                    value = (int16_t)(s[0] | (s[1] << 8));
                    break;
                case 3:
                    value = ((int32_t)((s[0] << 8) | (s[1] << 16) | (s[2] << 24))) >> 16;
                    break;
                case 4:
                    value = ((int32_t)(s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24))) >> 16;
                    break;
                default:
                    value = (int16_t)(s[0] | (s[1] << 8));
            }
            samples[c] = (int16_t)value;
        }
        out[i * 2] = samples[0];
        out[i * 2 + 1] = samples[1];
    }
}

void SendspinAudio::SendspinChunk(int64_t playAtLocalUs, const char* pcm, size_t size)
{
    Chunk chunk;
    chunk.playAtUs = playAtLocalUs;
    Convert(pcm, size, chunk.frames);
    if (chunk.frames.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fQueuedFrames += (int64_t)chunk.frames.size() / 2;
        fQueue.push_back(std::move(chunk));
    }
    fCondition.notify_one();
}

void SendspinAudio::SendspinStreamCleared()
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fQueue.clear();
        fQueuedFrames = 0;
        fGeneration++;
        fFlush = true;
    }
    fOutput.AbortWriters();
    fCondition.notify_all();
}

void SendspinAudio::SendspinStreamEnded()
{
    fStreamActive = false;
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fQueue.clear();
        fQueuedFrames = 0;
        fGeneration++;
        fFlush = true;
    }
    fOutput.AbortWriters();
    fCondition.notify_all();
    if (onStreamEnded)
        onStreamEnded();
}

void SendspinAudio::SendspinVolume(int volume)
{
    if (onVolume)
        onVolume(volume);
}

void SendspinAudio::SendspinMute(bool muted)
{
    if (onMute)
        onMute(muted);
}

void SendspinAudio::SendspinGroupState(const std::string& playbackState)
{
    if (onGroupState)
        onGroupState(playbackState);
}

void SendspinAudio::SendspinConnection(bool connected, const std::string& message)
{
    fConnected = connected;
    if (!connected)
        fStreamActive = false;
    if (onConnection)
        onConnection(connected, message);
}

void SendspinAudio::Run()
{
    int64_t expectedUs = 0;      // local time at which the next queued frame is scheduled
    int generation = -1;
    const int64_t kSlackUs = 40000; // tolerated timing error before correcting
    while (fRunning) {
        Chunk chunk;
        int chunkGeneration;
        {
            std::unique_lock<std::mutex> lock(fMutex);
            fCondition.wait_for(lock, std::chrono::milliseconds(100), [this] { return !fRunning || !fQueue.empty(); });
            if (!fRunning)
                break;
            if (fQueue.empty())
                continue;
            chunk = std::move(fQueue.front());
            fQueue.pop_front();
            fQueuedFrames -= (int64_t)chunk.frames.size() / 2;
            chunkGeneration = fGeneration;
            fFlush = false;
        }
        int rate = fFormat.sampleRate > 0 ? fFormat.sampleRate : 44100;
        if (!fOutput.IsOpen() || fOutput.SampleRate() != rate) {
            if (!fOutput.Open(rate)) {
                fprintf(stderr, "sendspin: cannot open audio output at %d Hz\n", rate);
                continue;
            }
        }
        if (chunkGeneration != generation) {
            generation = chunkGeneration;
            fOutput.Flush();
            expectedUs = 0;
        }
        size_t frames = chunk.frames.size() / 2;
        int64_t durationUs = (int64_t)frames * 1000000 / rate;
        int64_t now = system_time();
        // local time the next written frame will be heard
        int64_t bufferedUs = (int64_t)fOutput.BufferedFrames() * 1000000 / rate;
        int64_t nextHeardUs = now + fOutput.Latency() + bufferedUs;
        if (expectedUs == 0)
            expectedUs = chunk.playAtUs;
        int64_t error = chunk.playAtUs - nextHeardUs;
        if (error > kSlackUs) {
            // scheduled in the future: fill the gap with silence, waiting when the gap is large
            int64_t gap = error;
            if (gap > 3000000) {
                // far in the future (should not happen): wait instead of buffering huge silence
                snooze(gap - 2000000);
                gap = 2000000;
            }
            size_t silence = (size_t)(gap * rate / 1000000);
            std::vector<int16_t> zeros(silence * 2, 0);
            size_t offset = 0;
            while (offset < silence && fRunning && !fFlush) {
                size_t step = std::min<size_t>(silence - offset, 4096);
                if (!fOutput.Write(zeros.data(), step))
                    break;
                offset += step;
            }
        } else if (error < -kSlackUs) {
            // late: drop the part that should already have played
            size_t drop = (size_t)((-error) * rate / 1000000);
            if (drop >= frames)
                continue;
            chunk.frames.erase(chunk.frames.begin(), chunk.frames.begin() + drop * 2);
            frames -= drop;
        }
        if (fFlush)
            continue;
        fOutput.Write(chunk.frames.data(), frames);
        expectedUs = chunk.playAtUs + durationUs;
    }
}

} // namespace amp
