// DES and two-key triple DES (FIPS 46-3), the ciphers the NetMD download protocol is built on.
// Not for anything else: DES has been broken for decades, NetMD simply requires it.
#pragma once
#include <cstddef>
#include <cstdint>

namespace amp {

class Des {
public:
    explicit Des(const uint8_t key[8]);

    uint64_t Encrypt(uint64_t block) const { return Crypt(block, false); }
    uint64_t Decrypt(uint64_t block) const { return Crypt(block, true); }

    // Block helpers on big-endian byte buffers; `length` must be a multiple of 8.
    void EncryptEcb(const uint8_t* in, uint8_t* out, size_t length) const;
    void DecryptEcb(const uint8_t* in, uint8_t* out, size_t length) const;
    // CBC: `iv` is updated to the last ciphertext block, so consecutive calls continue one chain.
    void EncryptCbc(uint8_t iv[8], const uint8_t* in, uint8_t* out, size_t length) const;
    void DecryptCbc(uint8_t iv[8], const uint8_t* in, uint8_t* out, size_t length) const;

    static uint64_t Load(const uint8_t* bytes);
    static void Store(uint64_t value, uint8_t* bytes);

private:
    uint64_t Crypt(uint64_t block, bool decrypt) const;

    uint8_t fSubkeys[16][8]; // 16 rounds x 8 six-bit groups
};

// Two-key triple DES in EDE order: E(k1, D(k2, E(k1, x))). `key` holds k1 then k2.
class TripleDes {
public:
    explicit TripleDes(const uint8_t key[16]);
    uint64_t Encrypt(uint64_t block) const { return fFirst.Encrypt(fSecond.Decrypt(fFirst.Encrypt(block))); }
    void EncryptCbc(uint8_t iv[8], const uint8_t* in, uint8_t* out, size_t length) const;

private:
    Des fFirst, fSecond;
};

} // namespace amp
