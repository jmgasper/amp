// Apple Lossless packet decoder, for the same reason as FlacDecoder: the Media Kit demuxes the
// file and hands over packets and the codec's "magic cookie"; the decoding happens here.
// Mono and stereo streams only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amp {

class AlacDecoder {
public:
    // `cookie`: the ALACSpecificConfig, optionally wrapped in its 'alac' atom (or 'frma' + 'alac').
    bool Init(const uint8_t* cookie, size_t size);
    int SampleRate() const { return fSampleRate; }
    int Channels() const { return fChannels; }
    int BitsPerSample() const { return fBitsPerSample; }

    // Decodes one packet into `samples` (interleaved, at BitsPerSample()); returns the frame
    // count, or -1 on invalid data.
    int DecodePacket(const uint8_t* data, size_t size, std::vector<int32_t>& samples);

private:
    bool DecodeElement(class BitReader& reader, int channels, int channelBase, int& frames);
    void DecompressRice(class BitReader& reader, int32_t* out, int count, int bits, int historyMultiplier);

    uint32_t fFrameLength = 4096;
    int fBitsPerSample = 16;
    int fHistoryMultiplier = 40;
    int fInitialHistory = 10;
    int fRiceLimit = 14;
    int fChannels = 2;
    int fSampleRate = 44100;
    std::vector<int32_t> fOutput[2];
    std::vector<int32_t> fError[2];
    std::vector<int32_t> fExtraBits[2];
};

} // namespace amp
