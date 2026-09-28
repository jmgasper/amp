#include "Des.h"

namespace amp {

namespace {

// FIPS 46-3 tables. Bit numbers count from 1 at the most significant bit.
const uint8_t kInitialPermutation[64] = {
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7};
const uint8_t kFinalPermutation[64] = {
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25};
const uint8_t kRoundPermutation[32] = {
    16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25};
const uint8_t kPermutedChoice1[56] = {
    57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18,
    10, 2, 59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22,
    14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4};
const uint8_t kPermutedChoice2[48] = {
    14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10,
    23, 19, 12, 4, 26, 8, 16, 7, 27, 20, 13, 2,
    41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};
const uint8_t kShifts[16] = {1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};
const uint8_t kSBoxes[8][64] = {
    {14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
     0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
     4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
     15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13},
    {15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
     3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
     13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9},
    {10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
     13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
     13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
     1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12},
    {7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
     13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
     10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
     3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14},
    {2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
     14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
     4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
     11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3},
    {12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
     10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
     9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
     4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13},
    {4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
     13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
     1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
     6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12},
    {13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
     1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
     7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
     2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11}};

// Moves bit table[i] of an `inBits`-wide value to output bit i (both counted from the MSB).
uint64_t Permute(uint64_t value, const uint8_t* table, int outBits, int inBits)
{
    uint64_t result = 0;
    for (int i = 0; i < outBits; i++)
        result = (result << 1) | ((value >> (inBits - table[i])) & 1);
    return result;
}

// Precomputed lookups: the S-boxes fused with the round permutation, and the initial and
// final permutations split into one table per input byte.
struct Tables {
    uint32_t sp[8][64];
    uint64_t initial[8][256];
    uint64_t final[8][256];

    Tables()
    {
        for (int box = 0; box < 8; box++) {
            for (int x = 0; x < 64; x++) {
                int row = ((x >> 4) & 2) | (x & 1);
                int column = (x >> 1) & 15;
                uint32_t out = (uint32_t)kSBoxes[box][row * 16 + column] << (28 - 4 * box);
                sp[box][x] = (uint32_t)Permute(out, kRoundPermutation, 32, 32);
            }
        }
        for (int byte = 0; byte < 8; byte++) {
            for (int v = 0; v < 256; v++) {
                uint64_t in = (uint64_t)v << (56 - 8 * byte);
                initial[byte][v] = Permute(in, kInitialPermutation, 64, 64);
                final[byte][v] = Permute(in, kFinalPermutation, 64, 64);
            }
        }
    }
};

const Tables& GetTables()
{
    static const Tables tables;
    return tables;
}

inline uint64_t ApplyByteTables(const uint64_t (*table)[256], uint64_t value)
{
    uint64_t result = 0;
    for (int byte = 0; byte < 8; byte++)
        result |= table[byte][(value >> (56 - 8 * byte)) & 0xff];
    return result;
}

inline uint32_t RotateRight(uint32_t value, int count)
{
    count &= 31;
    return count == 0 ? value : (value >> count) | (value << (32 - count));
}

} // namespace

Des::Des(const uint8_t key[8])
{
    uint64_t permuted = Permute(Load(key), kPermutedChoice1, 56, 64);
    uint32_t c = (uint32_t)(permuted >> 28) & 0x0fffffff;
    uint32_t d = (uint32_t)permuted & 0x0fffffff;
    for (int round = 0; round < 16; round++) {
        for (int s = 0; s < kShifts[round]; s++) {
            c = ((c << 1) | (c >> 27)) & 0x0fffffff;
            d = ((d << 1) | (d >> 27)) & 0x0fffffff;
        }
        uint64_t subkey = Permute(((uint64_t)c << 28) | d, kPermutedChoice2, 48, 56);
        for (int group = 0; group < 8; group++)
            fSubkeys[round][group] = (uint8_t)((subkey >> (42 - 6 * group)) & 63);
    }
}

uint64_t Des::Crypt(uint64_t block, bool decrypt) const
{
    const Tables& tables = GetTables();
    uint64_t permuted = ApplyByteTables(tables.initial, block);
    uint32_t left = (uint32_t)(permuted >> 32);
    uint32_t right = (uint32_t)permuted;
    for (int round = 0; round < 16; round++) {
        const uint8_t* subkey = fSubkeys[decrypt ? 15 - round : round];
        // the expansion E makes group i out of bits 4i-1 .. 4i+4 of R (cyclic)
        uint32_t f = 0;
        for (int group = 0; group < 8; group++)
            f ^= tables.sp[group][(RotateRight(right, 27 - 4 * group) & 63) ^ subkey[group]];
        uint32_t next = left ^ f;
        left = right;
        right = next;
    }
    uint64_t preoutput = ((uint64_t)right << 32) | left;
    return ApplyByteTables(tables.final, preoutput);
}

uint64_t Des::Load(const uint8_t* bytes)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value = (value << 8) | bytes[i];
    return value;
}

void Des::Store(uint64_t value, uint8_t* bytes)
{
    for (int i = 7; i >= 0; i--) {
        bytes[i] = (uint8_t)value;
        value >>= 8;
    }
}

void Des::EncryptEcb(const uint8_t* in, uint8_t* out, size_t length) const
{
    for (size_t i = 0; i + 8 <= length; i += 8)
        Store(Encrypt(Load(in + i)), out + i);
}

void Des::DecryptEcb(const uint8_t* in, uint8_t* out, size_t length) const
{
    for (size_t i = 0; i + 8 <= length; i += 8)
        Store(Decrypt(Load(in + i)), out + i);
}

void Des::EncryptCbc(uint8_t iv[8], const uint8_t* in, uint8_t* out, size_t length) const
{
    uint64_t chain = Load(iv);
    for (size_t i = 0; i + 8 <= length; i += 8) {
        chain = Encrypt(Load(in + i) ^ chain);
        Store(chain, out + i);
    }
    Store(chain, iv);
}

void Des::DecryptCbc(uint8_t iv[8], const uint8_t* in, uint8_t* out, size_t length) const
{
    uint64_t chain = Load(iv);
    for (size_t i = 0; i + 8 <= length; i += 8) {
        uint64_t cipher = Load(in + i);
        Store(Decrypt(cipher) ^ chain, out + i);
        chain = cipher;
    }
    Store(chain, iv);
}

TripleDes::TripleDes(const uint8_t key[16])
    : fFirst(key), fSecond(key + 8)
{
}

void TripleDes::EncryptCbc(uint8_t iv[8], const uint8_t* in, uint8_t* out, size_t length) const
{
    uint64_t chain = Des::Load(iv);
    for (size_t i = 0; i + 8 <= length; i += 8) {
        chain = Encrypt(Des::Load(in + i) ^ chain);
        Des::Store(chain, out + i);
    }
    Des::Store(chain, iv);
}

} // namespace amp
