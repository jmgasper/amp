// MSB-first bit reader over a byte buffer, shared by the FLAC and ALAC packet decoders.
#pragma once
#include <cstddef>
#include <cstdint>

namespace amp {

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : fData(data), fSize(size) {}

    size_t BitsLeft() const { return fSize * 8 > fPosition ? fSize * 8 - fPosition : 0; }
    bool Overrun() const { return fPosition > fSize * 8; }
    size_t Position() const { return fPosition; }

    // Up to 32 bits, unsigned. Reading past the end yields zero bits and sets Overrun().
    uint32_t Read(int bits)
    {
        uint32_t value = 0;
        for (int i = 0; i < bits;) {
            size_t byte = fPosition >> 3;
            int offset = (int)(fPosition & 7);
            int take = 8 - offset;
            if (take > bits - i)
                take = bits - i;
            uint32_t chunk = byte < fSize ? ((fData[byte] >> (8 - offset - take)) & ((1u << take) - 1)) : 0;
            value = (take == 32 ? 0 : value << take) | chunk;
            i += take;
            fPosition += take;
        }
        return value;
    }

    int32_t ReadSigned(int bits)
    {
        if (bits == 0)
            return 0;
        uint32_t value = Read(bits);
        if (bits < 32 && (value & (1u << (bits - 1))))
            value |= ~0u << bits;
        return (int32_t)value;
    }

    bool ReadBit() { return Read(1) != 0; }

    uint32_t Peek(int bits)
    {
        size_t position = fPosition;
        uint32_t value = Read(bits);
        fPosition = position;
        return value;
    }

    // Counts zero bits up to the next one bit, which is consumed.
    uint32_t ReadUnaryZeros()
    {
        uint32_t count = 0;
        while (fPosition < fSize * 8) {
            size_t byte = fPosition >> 3;
            int offset = (int)(fPosition & 7);
            uint8_t rest = (uint8_t)(fData[byte] << offset);
            if (rest == 0) {
                count += 8 - offset;
                fPosition += 8 - offset;
                continue;
            }
            int zeros = __builtin_clz((uint32_t)rest << 24);
            count += zeros;
            fPosition += zeros + 1;
            return count;
        }
        fPosition = fSize * 8 + 1; // ran out: mark the overrun
        return count;
    }

    void Skip(size_t bits) { fPosition += bits; }
    void AlignToByte() { fPosition = (fPosition + 7) & ~(size_t)7; }

private:
    const uint8_t* fData;
    size_t fSize;
    size_t fPosition = 0;
};

} // namespace amp
