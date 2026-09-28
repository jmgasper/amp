#include "AlacDecoder.h"
#include "BitReader.h"
#include <algorithm>
#include <cstring>

namespace amp {

namespace {

enum { kElementSingle = 0, kElementPair = 1, kElementLfe = 3, kElementEnd = 7 };

int Log2(uint32_t value)
{
    return value ? 31 - __builtin_clz(value) : 0;
}

int SignOf(int32_t value)
{
    return value > 0 ? 1 : (value < 0 ? -1 : 0);
}

int32_t SignExtend(uint32_t value, int bits)
{
    int shift = 32 - bits;
    return shift <= 0 ? (int32_t)value : (int32_t)(value << shift) >> shift;
}

// One adaptive Golomb value: a unary prefix of one bits (nine means "escape, the value follows
// verbatim"), then k bits in which the values 0 and 1 are coded in k - 1 bits.
uint32_t ReadScalar(BitReader& reader, int k, int bits)
{
    uint32_t x = 0;
    while (x < 9 && reader.ReadBit())
        x++;
    if (x > 8)
        return reader.Read(bits);
    if (k != 1) {
        uint32_t extra = reader.Peek(k);
        x = (x << k) - x;
        if (extra > 1) {
            x += extra - 1;
            reader.Skip(k);
        } else
            reader.Skip(k - 1);
    }
    return x;
}

// ALAC's adaptive FIR prediction; the coefficients adapt as the samples are decoded.
void Predict(const int32_t* error, int32_t* out, int count, int bits, int16_t* coefficients, int order, int quant)
{
    if (count <= 0)
        return;
    out[0] = error[0];
    if (count == 1)
        return;
    if (order == 0) {
        memmove(out + 1, error + 1, (size_t)(count - 1) * sizeof(int32_t));
        return;
    }
    if (order == 31) {
        for (int i = 1; i < count; i++)
            out[i] = SignExtend((uint32_t)out[i - 1] + (uint32_t)error[i], bits);
        return;
    }
    int i = 1;
    for (; i <= order && i < count; i++)
        out[i] = SignExtend((uint32_t)out[i - 1] + (uint32_t)error[i], bits);
    for (; i < count; i++) {
        // coefficients[0] weighs the oldest of the `order` previous samples
        const int32_t* history = out + i - order;
        int32_t base = history[-1];
        uint32_t sum = 0;
        for (int j = 0; j < order; j++)
            sum += (uint32_t)(history[j] - base) * (uint32_t)(int32_t)coefficients[j];
        int32_t value = (int32_t)(((int64_t)(int32_t)sum + (1LL << (quant - 1))) >> quant);
        uint32_t errorValue = (uint32_t)error[i];
        out[i] = SignExtend((uint32_t)value + (uint32_t)base + errorValue, bits);
        int errorSign = SignOf((int32_t)errorValue);
        for (int j = 0; errorSign && j < order && (int32_t)(errorValue * (uint32_t)errorSign) > 0; j++) {
            int32_t delta = base - history[j];
            int sign = SignOf(delta) * errorSign;
            coefficients[j] = (int16_t)(coefficients[j] - sign);
            delta *= sign;
            errorValue -= (uint32_t)(delta >> quant) * (uint32_t)(j + 1);
        }
    }
}

} // namespace

bool AlacDecoder::Init(const uint8_t* cookie, size_t size)
{
    // skip an 'alac' atom header (size, tag, version and flags) in front of the config
    for (size_t i = 0; i + 4 <= size; i++) {
        if (memcmp(cookie + i, "alac", 4) == 0 && i + 8 + 24 <= size) {
            cookie += i + 8;
            size -= i + 8;
            break;
        }
    }
    if (size < 24)
        return false;
    BitReader reader(cookie, size);
    fFrameLength = reader.Read(32);
    reader.Read(8); // compatible version
    fBitsPerSample = (int)reader.Read(8);
    fHistoryMultiplier = (int)reader.Read(8);
    fInitialHistory = (int)reader.Read(8);
    fRiceLimit = (int)reader.Read(8);
    fChannels = (int)reader.Read(8);
    reader.Read(16); // longest zero run
    reader.Read(32); // largest packet
    reader.Read(32); // average bit rate
    fSampleRate = (int)reader.Read(32);
    return fFrameLength > 0 && fFrameLength <= 65536 && fBitsPerSample >= 8 && fBitsPerSample <= 32
        && fChannels >= 1 && fChannels <= 2 && fSampleRate > 0;
}

void AlacDecoder::DecompressRice(BitReader& reader, int32_t* out, int count, int bits, int historyMultiplier)
{
    uint32_t history = (uint32_t)fInitialHistory;
    uint32_t signModifier = 0;
    for (int i = 0; i < count; i++) {
        int k = std::min(Log2((history >> 9) + 3), fRiceLimit);
        uint32_t x = ReadScalar(reader, k, bits) + signModifier;
        signModifier = 0;
        out[i] = (int32_t)(x >> 1) ^ -(int32_t)(x & 1);
        if (x > 0xffff)
            history = 0xffff;
        else
            history += x * historyMultiplier - ((history * historyMultiplier) >> 9);
        // after quiet stretches, runs of zeros are coded as a count
        if (history < 128 && i + 1 < count) {
            k = std::min(7 - Log2(history) + (int)((history + 16) >> 6), fRiceLimit);
            uint32_t run = ReadScalar(reader, k, 16);
            if (run > 0) {
                run = std::min<uint32_t>(run, (uint32_t)(count - i - 1));
                memset(out + i + 1, 0, run * sizeof(int32_t));
                i += (int)run;
            }
            if (run <= 0xffff)
                signModifier = 1;
            history = 0;
        }
    }
}

bool AlacDecoder::DecodeElement(BitReader& reader, int channels, int channelBase, int& frames)
{
    reader.Read(4);  // element instance tag
    reader.Read(12); // unused
    bool hasSize = reader.ReadBit();
    int extraBits = (int)reader.Read(2) << 3;
    int bits = fBitsPerSample - extraBits + channels - 1;
    if (bits > 32 || bits < 1)
        return false;
    bool compressed = !reader.ReadBit();
    int count = hasSize ? (int)reader.Read(32) : (int)fFrameLength;
    if (count <= 0 || (uint32_t)count > fFrameLength)
        return false;
    if (frames >= 0 && frames != count)
        return false; // every element of a packet has the same length
    frames = count;
    for (int ch = 0; ch < channels; ch++) {
        fOutput[channelBase + ch].resize(count);
        fError[ch].resize(count);
        fExtraBits[ch].resize(count);
    }
    int shift = 0, weight = 0;
    if (compressed) {
        shift = (int)reader.Read(8);
        weight = (int)reader.Read(8);
        int predictionType[2], quant[2], multiplier[2], order[2];
        int16_t coefficients[2][32];
        for (int ch = 0; ch < channels; ch++) {
            predictionType[ch] = (int)reader.Read(4);
            quant[ch] = (int)reader.Read(4);
            multiplier[ch] = (int)reader.Read(3);
            order[ch] = (int)reader.Read(5);
            if (quant[ch] == 0)
                return false;
            for (int i = order[ch] - 1; i >= 0; i--)
                coefficients[ch][i] = (int16_t)reader.ReadSigned(16);
        }
        if (extraBits)
            for (int i = 0; i < count; i++)
                for (int ch = 0; ch < channels; ch++)
                    fExtraBits[ch][i] = (int32_t)reader.Read(extraBits);
        for (int ch = 0; ch < channels; ch++) {
            DecompressRice(reader, fError[ch].data(), count, bits, multiplier[ch] * fHistoryMultiplier / 4);
            if (predictionType[ch] == 15)
                Predict(fError[ch].data(), fError[ch].data(), count, bits, nullptr, 31, 0);
            Predict(fError[ch].data(), fOutput[channelBase + ch].data(), count, bits, coefficients[ch], order[ch], quant[ch]);
        }
    } else {
        for (int i = 0; i < count; i++)
            for (int ch = 0; ch < channels; ch++)
                fOutput[channelBase + ch][i] = reader.ReadSigned(fBitsPerSample);
        extraBits = 0;
    }
    if (reader.Overrun())
        return false;
    if (channels == 2 && weight) {
        int32_t* left = fOutput[channelBase].data();
        int32_t* right = fOutput[channelBase + 1].data();
        for (int i = 0; i < count; i++) {
            int32_t a = left[i], b = right[i];
            a -= (int32_t)(((int64_t)b * weight) >> shift);
            b += a;
            left[i] = b;
            right[i] = a;
        }
    }
    if (extraBits)
        for (int ch = 0; ch < channels; ch++)
            for (int i = 0; i < count; i++)
                fOutput[channelBase + ch][i] = (int32_t)((uint32_t)fOutput[channelBase + ch][i] << extraBits) | fExtraBits[ch][i];
    return true;
}

int AlacDecoder::DecodePacket(const uint8_t* data, size_t size, std::vector<int32_t>& samples)
{
    BitReader reader(data, size);
    int frames = -1;
    int channel = 0;
    while (reader.BitsLeft() >= 3) {
        int element = (int)reader.Read(3);
        if (element == kElementEnd)
            break;
        if (element != kElementSingle && element != kElementPair && element != kElementLfe)
            return -1;
        int channels = element == kElementPair ? 2 : 1;
        if (channel + channels > fChannels)
            return -1;
        if (!DecodeElement(reader, channels, channel, frames))
            return -1;
        channel += channels;
    }
    if (frames <= 0 || channel != fChannels)
        return -1;
    size_t base = samples.size();
    samples.resize(base + (size_t)frames * fChannels);
    int32_t* out = samples.data() + base;
    for (int i = 0; i < frames; i++)
        for (int ch = 0; ch < fChannels; ch++)
            *out++ = fOutput[ch][i];
    return frames;
}

} // namespace amp
