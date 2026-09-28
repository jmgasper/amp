// FLAC frame decoder (RFC 9639). The container is left to the Media Kit: Haiku's own FLAC
// decoding drops the last frames of every file on machines with many cores, so Amp demuxes
// with the Media Kit and decodes the frames here.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amp {

class FlacDecoder {
public:
    // `streamInfo`: the 34-byte STREAMINFO block (a leading "fLaC" and block header are skipped).
    bool Init(const uint8_t* streamInfo, size_t size);
    int SampleRate() const { return fSampleRate; }
    int Channels() const { return fChannels; }
    int BitsPerSample() const { return fBitsPerSample; }

    // Decodes one frame into `samples` (interleaved, at BitsPerSample()); returns the frame
    // count, or -1 when the data is not a valid frame.
    int DecodeFrame(const uint8_t* data, size_t size, std::vector<int32_t>& samples);

private:
    bool DecodeSubframe(class BitReader& reader, int bits, int blockSize, int32_t* out);
    bool DecodeResidual(class BitReader& reader, int order, int blockSize, int32_t* out);

    int fSampleRate = 0;
    int fChannels = 0;
    int fBitsPerSample = 0;
    std::vector<int32_t> fChannelBuffers[8];
};

} // namespace amp
