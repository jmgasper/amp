#include "MiniDiscPcm.h"
#include "core/AlacDecoder.h"
#include "core/FlacDecoder.h"
#include "core/Model.h"
#include "core/Resampler.h"
#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <MediaDefs.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace amp {

namespace {

const int kOutputRate = 44100;

// Writes float stereo frames as 16-bit big-endian. Samples that already sit on a 16-bit step
// (a 16-bit source that needed no resampling) pass through untouched; the rest get TPDF dither.
class PcmWriter {
public:
    explicit PcmWriter(FILE* file) : fFile(file) {}

    bool Write(const float* frames, size_t count)
    {
        fBytes.resize(count * 4);
        uint8_t* out = fBytes.data();
        for (size_t i = 0; i < count * 2; i++) {
            float scaled = frames[i] * 32768.0f;
            float rounded = std::nearbyint(scaled);
            if (scaled != rounded)
                rounded = std::nearbyint(scaled + fDither(fRandom) - fDither(fRandom));
            int value = (int)std::max(-32768.0f, std::min(32767.0f, rounded));
            *out++ = (uint8_t)(value >> 8);
            *out++ = (uint8_t)value;
        }
        if (fwrite(fBytes.data(), 1, fBytes.size(), fFile) != fBytes.size())
            return false;
        fWritten += fBytes.size();
        return true;
    }

    int64_t Written() const { return fWritten; }

private:
    FILE* fFile;
    std::vector<uint8_t> fBytes;
    std::minstd_rand fRandom{12345};
    std::uniform_real_distribution<float> fDither{0.0f, 1.0f};
    int64_t fWritten = 0;
};

// Converts one decoded buffer to interleaved float stereo.
void ToStereoFloat(const void* data, int64 frames, const media_raw_audio_format& format, std::vector<float>& out)
{
    int channels = std::max(1, (int)format.channel_count);
    out.resize((size_t)frames * 2);
    auto sample = [&](int64 frame, int channel) -> float {
        int64 index = frame * channels + channel;
        switch (format.format) {
            case media_raw_audio_format::B_AUDIO_FLOAT:
                return static_cast<const float*>(data)[index];
            case media_raw_audio_format::B_AUDIO_DOUBLE:
                return (float)static_cast<const double*>(data)[index];
            case media_raw_audio_format::B_AUDIO_INT:
                return static_cast<const int32*>(data)[index] / 2147483648.0f;
            case media_raw_audio_format::B_AUDIO_SHORT:
                return static_cast<const int16*>(data)[index] / 32768.0f;
            case media_raw_audio_format::B_AUDIO_CHAR:
                return static_cast<const int8*>(data)[index] / 128.0f;
            case media_raw_audio_format::B_AUDIO_UCHAR:
                return ((int)static_cast<const uint8*>(data)[index] - 128) / 128.0f;
        }
        return 0.0f;
    };
    for (int64 i = 0; i < frames; i++) {
        float left = sample(i, 0);
        float right = channels > 1 ? sample(i, 1) : left;
        if (channels >= 5) {
            // 5.1 order L R C LFE Ls Rs: fold the centre and surrounds in at -3 dB
            float center = sample(i, 2) * 0.7071f;
            left = (left + center + sample(i, 4) * 0.7071f) / 2.4142f;
            right = (right + center + sample(i, 5 < channels ? 5 : 4) * 0.7071f) / 2.4142f;
        }
        out[(size_t)i * 2] = left;
        out[(size_t)i * 2 + 1] = right;
    }
}

// Delivers a file's audio as float stereo frames.
class StereoSource {
public:
    virtual ~StereoSource() {}
    virtual int Rate() const = 0;
    virtual int64 TotalFrames() const = 0;
    // Replaces `frames` with the next block (interleaved stereo); false at the end or on error.
    virtual bool Next(std::vector<float>& frames, std::string& error) = 0;
};

// Everything the Media Kit decodes.
class MediaKitSource : public StereoSource {
public:
    bool Init(BMediaTrack* track, std::string& error)
    {
        fTrack = track;
        media_format format;
        format.type = B_MEDIA_RAW_AUDIO;
        format.u.raw_audio = media_raw_audio_format::wildcard;
        format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
        format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
        if (track->DecodedFormat(&format) != B_OK) {
            error = "cannot decode the audio";
            return false;
        }
        fRaw = format.u.raw_audio;
        fRate = fRaw.frame_rate > 0 ? (int)std::lround(fRaw.frame_rate) : kOutputRate;
        fBuffer.resize((std::max<size_t>(fRaw.buffer_size, 4096) + 4096) * 2);
        return true;
    }
    int Rate() const override { return fRate; }
    int64 TotalFrames() const override { return fTrack->CountFrames(); }
    bool Next(std::vector<float>& frames, std::string& error) override
    {
        int errors = 0;
        size_t frameBytes = std::max(1, (int)fRaw.channel_count) * (fRaw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK);
        while (true) {
            int64 count = 0;
            media_header header;
            status_t status = fTrack->ReadFrames(fBuffer.data(), &count, &header);
            if (count > 0 && (size_t)count * frameBytes <= fBuffer.size()) {
                ToStereoFloat(fBuffer.data(), count, fRaw, frames);
                return true;
            }
            if (status == B_LAST_BUFFER_ERROR)
                return false;
            if (status != B_OK && ++errors > 8) {
                error = "the file cannot be decoded";
                return false;
            }
        }
    }

private:
    BMediaTrack* fTrack = nullptr;
    media_raw_audio_format fRaw;
    int fRate = kOutputRate;
    std::vector<char> fBuffer;
};

// FLAC and ALAC: the Media Kit only demuxes, Amp decodes (see FlacDecoder.h for why).
class LosslessSource : public StereoSource {
public:
    // False when the track is neither FLAC nor ALAC, or its setup data is unusable.
    bool Init(BMediaTrack* track)
    {
        media_codec_info info;
        media_format encoded;
        if (track->GetCodecInfo(&info) != B_OK || track->EncodedFormat(&encoded) != B_OK)
            return false;
        std::string name = ToLower(std::string(info.short_name) + " " + info.pretty_name);
        const uint8_t* setup = static_cast<const uint8_t*>(encoded.MetaData());
        size_t setupSize = encoded.MetaDataSize() > 0 ? (size_t)encoded.MetaDataSize() : 0;
        if (!setup || setupSize == 0)
            return false;
        if (name.find("flac") != std::string::npos && fFlac.Init(setup, setupSize)) {
            fIsFlac = true;
            fRate = fFlac.SampleRate();
            fBits = fFlac.BitsPerSample();
        } else if (name.find("alac") != std::string::npos || name.find("apple lossless") != std::string::npos) {
            if (!fAlac.Init(setup, setupSize))
                return false;
            fRate = fAlac.SampleRate();
            fBits = fAlac.BitsPerSample();
        } else
            return false;
        fTrack = track;
        return fRate > 0;
    }
    int Rate() const override { return fRate; }
    int64 TotalFrames() const override { return fTrack->CountFrames(); }
    bool Next(std::vector<float>& frames, std::string& error) override
    {
        while (true) {
            char* chunk = nullptr;
            int32 size = 0;
            media_header header;
            if (fTrack->ReadChunk(&chunk, &size, &header) != B_OK)
                return false;
            if (!chunk || size <= 0)
                continue;
            fSamples.clear();
            const uint8_t* data = reinterpret_cast<const uint8_t*>(chunk);
            int count = fIsFlac ? fFlac.DecodeFrame(data, (size_t)size, fSamples)
                : fAlac.DecodePacket(data, (size_t)size, fSamples);
            if (count < 0) {
                error = fIsFlac ? "damaged FLAC data" : "damaged Apple Lossless data";
                return false;
            }
            if (count == 0)
                continue;
            int channels = (int)(fSamples.size() / count);
            float scale = 1.0f / (float)(1u << (fBits - 1));
            frames.resize((size_t)count * 2);
            for (int i = 0; i < count; i++) {
                float left = fSamples[(size_t)i * channels] * scale;
                frames[(size_t)i * 2] = left;
                frames[(size_t)i * 2 + 1] = channels > 1 ? fSamples[(size_t)i * channels + 1] * scale : left;
            }
            return true;
        }
    }

private:
    BMediaTrack* fTrack = nullptr;
    FlacDecoder fFlac;
    AlacDecoder fAlac;
    bool fIsFlac = false;
    int fRate = 0;
    int fBits = 16;
    std::vector<int32_t> fSamples;
};

} // namespace

int64_t ConvertToMiniDiscPcm(const std::string& path, const std::string& outputPath,
    const std::function<bool(float)>& progress, std::string& error)
{
    entry_ref ref;
    if (get_ref_for_path(path.c_str(), &ref) != B_OK) {
        error = "file not found";
        return -1;
    }
    BMediaFile file(&ref);
    if (file.InitCheck() != B_OK) {
        error = "unsupported file format";
        return -1;
    }
    BMediaTrack* track = nullptr;
    for (int32 i = 0; i < file.CountTracks() && !track; i++) {
        BMediaTrack* candidate = file.TrackAt(i);
        media_format encoded;
        if (candidate && candidate->EncodedFormat(&encoded) == B_OK && encoded.IsAudio())
            track = candidate;
        else if (candidate)
            file.ReleaseTrack(candidate);
    }
    if (!track) {
        error = "no audio in the file";
        return -1;
    }
    std::unique_ptr<BMediaTrack, std::function<void(BMediaTrack*)>> trackGuard(track,
        [&file](BMediaTrack* t) { file.ReleaseTrack(t); });

    std::unique_ptr<StereoSource> source;
    std::unique_ptr<LosslessSource> lossless(new LosslessSource());
    if (lossless->Init(track))
        source = std::move(lossless);
    else {
        std::unique_ptr<MediaKitSource> media(new MediaKitSource());
        if (!media->Init(track, error))
            return -1;
        source = std::move(media);
    }
    int64 totalFrames = source->TotalFrames();

    FILE* out = fopen(outputPath.c_str(), "wb");
    if (!out) {
        error = "cannot create a temporary file";
        return -1;
    }
    setvbuf(out, nullptr, _IOFBF, 256 * 1024);
    PcmWriter writer(out);
    std::unique_ptr<Resampler> resampler;
    if (source->Rate() != kOutputRate)
        resampler.reset(new Resampler(source->Rate(), kOutputRate, 2));
    std::vector<float> stereo, resampled;
    int64 decoded = 0;
    bool ok = true;
    while (source->Next(stereo, error)) {
        size_t frames = stereo.size() / 2;
        if (resampler) {
            resampled.clear();
            resampler->Process(stereo.data(), frames, resampled);
            ok = writer.Write(resampled.data(), resampled.size() / 2);
        } else
            ok = writer.Write(stereo.data(), frames);
        if (!ok) {
            error = "cannot write the temporary file (disk full?)";
            break;
        }
        decoded += (int64)frames;
        if (progress && !progress(totalFrames > 0 ? std::min(1.0f, (float)decoded / totalFrames) : 0.0f)) {
            error = "cancelled";
            ok = false;
            break;
        }
    }
    if (ok && !error.empty())
        ok = false; // the source failed
    if (ok && resampler) {
        resampled.clear();
        resampler->Flush(resampled);
        ok = writer.Write(resampled.data(), resampled.size() / 2);
        if (!ok)
            error = "cannot write the temporary file (disk full?)";
    }
    if (fclose(out) != 0 && ok) {
        error = "cannot write the temporary file";
        ok = false;
    }
    if (ok && writer.Written() == 0) {
        error = "the file contains no audio";
        ok = false;
    }
    if (!ok) {
        remove(outputPath.c_str());
        return -1;
    }
    return writer.Written();
}

PcmFileSource::PcmFileSource(const std::string& path)
    : fFile(fopen(path.c_str(), "rb"))
{
    if (fFile)
        setvbuf(fFile, nullptr, _IOFBF, 256 * 1024);
}

PcmFileSource::~PcmFileSource()
{
    if (fFile)
        fclose(fFile);
}

size_t PcmFileSource::Read(uint8_t* buffer, size_t length)
{
    return fFile ? fread(buffer, 1, length, fFile) : 0;
}

} // namespace amp
