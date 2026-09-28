#include "FlacDecoder.h"
#include "BitReader.h"
#include <cstring>

namespace amp {

bool FlacDecoder::Init(const uint8_t* data, size_t size)
{
    if (size >= 4 && memcmp(data, "fLaC", 4) == 0) {
        data += 4;
        size -= 4;
    }
    if (size >= 38 && (data[0] & 0x7f) == 0) { // a metadata block header precedes the block
        data += 4;
        size -= 4;
    }
    if (size < 34)
        return false;
    BitReader reader(data, size);
    reader.Skip(16 + 16 + 24 + 24); // block sizes and frame sizes
    fSampleRate = (int)reader.Read(20);
    fChannels = (int)reader.Read(3) + 1;
    fBitsPerSample = (int)reader.Read(5) + 1;
    return fSampleRate > 0 && fBitsPerSample >= 4;
}

int FlacDecoder::DecodeFrame(const uint8_t* data, size_t size, std::vector<int32_t>& samples)
{
    BitReader reader(data, size);
    if (reader.Read(15) != 0x7ffc)
        return -1;
    reader.Read(1); // blocking strategy
    int blockCode = (int)reader.Read(4);
    int rateCode = (int)reader.Read(4);
    int assignment = (int)reader.Read(4);
    int sizeCode = (int)reader.Read(3);
    reader.Read(1);
    // the frame or sample number, UTF-8 style
    uint32_t first = reader.Read(8);
    int extra = 0;
    for (uint32_t mask = 0x80; mask && (first & mask); mask >>= 1)
        extra++;
    if (extra == 1 || extra > 7)
        return -1;
    for (int i = 1; i < extra; i++)
        reader.Read(8);
    int blockSize = 0;
    if (blockCode == 1)
        blockSize = 192;
    else if (blockCode >= 2 && blockCode <= 5)
        blockSize = 576 << (blockCode - 2);
    else if (blockCode == 6)
        blockSize = (int)reader.Read(8) + 1;
    else if (blockCode == 7)
        blockSize = (int)reader.Read(16) + 1;
    else if (blockCode >= 8)
        blockSize = 256 << (blockCode - 8);
    else
        return -1;
    if (rateCode == 12)
        reader.Read(8);
    else if (rateCode == 13 || rateCode == 14)
        reader.Read(16);
    reader.Read(8); // header CRC

    static const int kSampleSizes[8] = {0, 8, 12, 0, 16, 20, 24, 32};
    int bits = sizeCode == 0 ? fBitsPerSample : kSampleSizes[sizeCode];
    if (bits == 0)
        return -1;
    int channels = assignment < 8 ? assignment + 1 : 2;
    if (assignment > 10 || channels > 8)
        return -1;
    for (int ch = 0; ch < channels; ch++) {
        fChannelBuffers[ch].resize(blockSize);
        // the side channel of a stereo decorrelation carries one extra bit
        bool side = (assignment == 8 && ch == 1) || (assignment == 9 && ch == 0) || (assignment == 10 && ch == 1);
        if (!DecodeSubframe(reader, bits + (side ? 1 : 0), blockSize, fChannelBuffers[ch].data()))
            return -1;
    }
    if (reader.Overrun())
        return -1;
    int32_t* left = fChannelBuffers[0].data();
    int32_t* right = channels > 1 ? fChannelBuffers[1].data() : nullptr;
    for (int i = 0; i < blockSize && assignment >= 8; i++) {
        int32_t a = left[i], b = right[i];
        if (assignment == 8) {          // left, side
            right[i] = a - b;
        } else if (assignment == 9) {   // side, right
            left[i] = a + b;
        } else {                        // mid, side
            int64_t mid = ((int64_t)a << 1) | (b & 1);
            left[i] = (int32_t)((mid + b) >> 1);
            right[i] = (int32_t)((mid - b) >> 1);
        }
    }
    size_t base = samples.size();
    samples.resize(base + (size_t)blockSize * channels);
    int32_t* out = samples.data() + base;
    for (int i = 0; i < blockSize; i++)
        for (int ch = 0; ch < channels; ch++)
            *out++ = fChannelBuffers[ch][i];
    fChannels = channels;
    fBitsPerSample = bits;
    return blockSize;
}

bool FlacDecoder::DecodeSubframe(BitReader& reader, int bits, int blockSize, int32_t* out)
{
    if (reader.ReadBit())
        return false; // padding bit must be zero
    int type = (int)reader.Read(6);
    int wasted = 0;
    if (reader.ReadBit())
        wasted = (int)reader.ReadUnaryZeros() + 1;
    bits -= wasted;
    if (bits <= 0 || bits > 32)
        return false;
    if (type == 0) {
        int32_t value = reader.ReadSigned(bits);
        for (int i = 0; i < blockSize; i++)
            out[i] = value;
    } else if (type == 1) {
        for (int i = 0; i < blockSize; i++)
            out[i] = reader.ReadSigned(bits);
    } else if (type >= 8 && type <= 12) {
        int order = type - 8;
        if (order > blockSize)
            return false;
        for (int i = 0; i < order; i++)
            out[i] = reader.ReadSigned(bits);
        if (!DecodeResidual(reader, order, blockSize, out))
            return false;
        for (int i = order; i < blockSize; i++) {
            int64_t prediction = 0;
            switch (order) {
                case 1: prediction = out[i - 1]; break;
                case 2: prediction = 2LL * out[i - 1] - out[i - 2]; break;
                case 3: prediction = 3LL * out[i - 1] - 3LL * out[i - 2] + out[i - 3]; break;
                case 4: prediction = 4LL * out[i - 1] - 6LL * out[i - 2] + 4LL * out[i - 3] - out[i - 4]; break;
            }
            out[i] = (int32_t)(prediction + out[i]);
        }
    } else if (type >= 32) {
        int order = type - 31;
        if (order > blockSize)
            return false;
        for (int i = 0; i < order; i++)
            out[i] = reader.ReadSigned(bits);
        int precision = (int)reader.Read(4) + 1;
        if (precision == 16)
            return false;
        int shift = reader.ReadSigned(5);
        if (shift < 0)
            return false;
        int32_t coefficients[32];
        for (int i = 0; i < order; i++)
            coefficients[i] = reader.ReadSigned(precision);
        if (!DecodeResidual(reader, order, blockSize, out))
            return false;
        for (int i = order; i < blockSize; i++) {
            int64_t sum = 0;
            for (int j = 0; j < order; j++)
                sum += (int64_t)coefficients[j] * out[i - 1 - j];
            out[i] = (int32_t)(out[i] + (sum >> shift));
        }
    } else
        return false;
    if (wasted)
        for (int i = 0; i < blockSize; i++)
            out[i] = (int32_t)((uint32_t)out[i] << wasted);
    return !reader.Overrun();
}

bool FlacDecoder::DecodeResidual(BitReader& reader, int order, int blockSize, int32_t* out)
{
    int method = (int)reader.Read(2);
    if (method > 1)
        return false;
    int parameterBits = method == 0 ? 4 : 5;
    int escape = method == 0 ? 15 : 31;
    int partitionOrder = (int)reader.Read(4);
    int partitions = 1 << partitionOrder;
    int perPartition = blockSize >> partitionOrder;
    if ((perPartition << partitionOrder) != blockSize || perPartition < order)
        return false;
    int index = order;
    for (int p = 0; p < partitions; p++) {
        int count = p == 0 ? perPartition - order : perPartition;
        int parameter = (int)reader.Read(parameterBits);
        if (parameter == escape) {
            int raw = (int)reader.Read(5);
            for (int i = 0; i < count; i++)
                out[index++] = reader.ReadSigned(raw);
        } else {
            for (int i = 0; i < count; i++) {
                uint32_t quotient = reader.ReadUnaryZeros();
                uint32_t value = (quotient << parameter) | reader.Read(parameter);
                out[index++] = (int32_t)(value >> 1) ^ -(int32_t)(value & 1);
            }
        }
        if (reader.Overrun())
            return false;
    }
    return true;
}

} // namespace amp
