#include "crypto_secure.h"
#include <cstring>
#include <algorithm>

namespace SecureCrypto {

// =========================================================================
// 1. AES-256 Implementation (CTR Mode)
// =========================================================================

static const uint8_t AES_SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint32_t AES_RCON[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36
};

static inline uint32_t rotWord(uint32_t w) {
    return ((w << 8) & 0xFFFFFFFF) | (w >> 24);
}

static inline uint32_t subWord(uint32_t w) {
    return (uint32_t(AES_SBOX[(w >> 24) & 0xFF]) << 24) |
           (uint32_t(AES_SBOX[(w >> 16) & 0xFF]) << 16) |
           (uint32_t(AES_SBOX[(w >> 8) & 0xFF]) << 8) |
           uint32_t(AES_SBOX[w & 0xFF]);
}

static void aesKeyExpansion256(const uint8_t key[32], uint32_t roundKeys[60]) {
    for (int i = 0; i < 8; i++) {
        roundKeys[i] = (uint32_t(key[4 * i]) << 24) |
                       (uint32_t(key[4 * i + 1]) << 16) |
                       (uint32_t(key[4 * i + 2]) << 8) |
                       uint32_t(key[4 * i + 3]);
    }
    for (int i = 8; i < 60; i++) {
        uint32_t temp = roundKeys[i - 1];
        if (i % 8 == 0) {
            temp = subWord(rotWord(temp)) ^ (AES_RCON[i / 8] << 24);
        } else if (i % 8 == 4) {
            temp = subWord(temp);
        }
        roundKeys[i] = roundKeys[i - 8] ^ temp;
    }
}

static inline uint8_t xtime(uint8_t a) {
    return (a & 0x80) ? uint8_t((a << 1) ^ 0x1B) : uint8_t(a << 1);
}

static void aesEncryptBlock(const uint8_t in[16], uint8_t out[16], const uint32_t roundKeys[60]) {
    uint8_t state[16];
    std::memcpy(state, in, 16);

    // Initial AddRoundKey
    for (int c = 0; c < 4; c++) {
        uint32_t rk = roundKeys[c];
        state[c * 4 + 0] ^= uint8_t((rk >> 24) & 0xFF);
        state[c * 4 + 1] ^= uint8_t((rk >> 16) & 0xFF);
        state[c * 4 + 2] ^= uint8_t((rk >> 8) & 0xFF);
        state[c * 4 + 3] ^= uint8_t(rk & 0xFF);
    }

    // 13 Main Rounds
    for (int r = 1; r < 14; r++) {
        // SubBytes
        for (int i = 0; i < 16; i++) {
            state[i] = AES_SBOX[state[i]];
        }
        // ShiftRows
        uint8_t t0 = state[0], t1 = state[5], t2 = state[10], t3 = state[15];
        uint8_t t4 = state[4], t5 = state[9], t6 = state[14], t7 = state[3];
        uint8_t t8 = state[8], t9 = state[13], t10 = state[2], t11 = state[7];
        uint8_t t12 = state[12], t13 = state[1], t14 = state[6], t15 = state[11];
        state[0] = t0; state[1] = t1; state[2] = t2; state[3] = t3;
        state[4] = t4; state[5] = t5; state[6] = t6; state[7] = t7;
        state[8] = t8; state[9] = t9; state[10] = t10; state[11] = t11;
        state[12] = t12; state[13] = t13; state[14] = t14; state[15] = t15;

        // MixColumns
        for (int c = 0; c < 4; c++) {
            int i = c * 4;
            uint8_t s0 = state[i], s1 = state[i + 1], s2 = state[i + 2], s3 = state[i + 3];
            state[i + 0] = xtime(s0) ^ (xtime(s1) ^ s1) ^ s2 ^ s3;
            state[i + 1] = s0 ^ xtime(s1) ^ (xtime(s2) ^ s2) ^ s3;
            state[i + 2] = s0 ^ s1 ^ xtime(s2) ^ (xtime(s3) ^ s3);
            state[i + 3] = (xtime(s0) ^ s0) ^ s1 ^ s2 ^ xtime(s3);
        }

        // AddRoundKey
        for (int c = 0; c < 4; c++) {
            uint32_t rk = roundKeys[r * 4 + c];
            state[c * 4 + 0] ^= uint8_t((rk >> 24) & 0xFF);
            state[c * 4 + 1] ^= uint8_t((rk >> 16) & 0xFF);
            state[c * 4 + 2] ^= uint8_t((rk >> 8) & 0xFF);
            state[c * 4 + 3] ^= uint8_t(rk & 0xFF);
        }
    }

    // Final Round 14 (SubBytes, ShiftRows, AddRoundKey - no MixColumns)
    for (int i = 0; i < 16; i++) {
        state[i] = AES_SBOX[state[i]];
    }
    uint8_t t0 = state[0], t1 = state[5], t2 = state[10], t3 = state[15];
    uint8_t t4 = state[4], t5 = state[9], t6 = state[14], t7 = state[3];
    uint8_t t8 = state[8], t9 = state[13], t10 = state[2], t11 = state[7];
    uint8_t t12 = state[12], t13 = state[1], t14 = state[6], t15 = state[11];
    state[0] = t0; state[1] = t1; state[2] = t2; state[3] = t3;
    state[4] = t4; state[5] = t5; state[6] = t6; state[7] = t7;
    state[8] = t8; state[9] = t9; state[10] = t10; state[11] = t11;
    state[12] = t12; state[13] = t13; state[14] = t14; state[15] = t15;

    for (int c = 0; c < 4; c++) {
        uint32_t rk = roundKeys[14 * 4 + c];
        state[c * 4 + 0] ^= uint8_t((rk >> 24) & 0xFF);
        state[c * 4 + 1] ^= uint8_t((rk >> 16) & 0xFF);
        state[c * 4 + 2] ^= uint8_t((rk >> 8) & 0xFF);
        state[c * 4 + 3] ^= uint8_t(rk & 0xFF);
    }

    std::memcpy(out, state, 16);
}

static void aes256CtrProcess(const uint8_t* in, uint8_t* out, size_t len,
                             const uint8_t key[32], const uint8_t iv[16]) {
    uint32_t roundKeys[60];
    aesKeyExpansion256(key, roundKeys);

    uint8_t counter[16];
    std::memcpy(counter, iv, 16);

    uint8_t keystream[16];
    for (size_t i = 0; i < len; i += 16) {
        aesEncryptBlock(counter, keystream, roundKeys);
        size_t blockLen = std::min((size_t)16, len - i);
        for (size_t j = 0; j < blockLen; j++) {
            out[i + j] = in[i + j] ^ keystream[j];
        }

        // Increment 128-bit big-endian counter
        for (int k = 15; k >= 0; k--) {
            counter[k]++;
            if (counter[k] != 0) break;
        }
    }
}

// =========================================================================
// 2. ChaCha20 Implementation (RFC 8439)
// =========================================================================

static inline uint32_t rotl32(uint32_t v, int n) {
    return ((v << n) & 0xFFFFFFFF) | (v >> (32 - n));
}

#define CHACHA_QR(a, b, c, d) \
    a += b; d ^= a; d = rotl32(d, 16); \
    c += d; b ^= c; b = rotl32(b, 12); \
    a += b; d ^= a; d = rotl32(d, 8);  \
    c += d; b ^= c; b = rotl32(b, 7);

static void chacha20Block(uint8_t out[64], const uint32_t state[16]) {
    uint32_t x[16];
    std::memcpy(x, state, sizeof(x));

    for (int i = 0; i < 10; i++) {
        // Column rounds
        CHACHA_QR(x[0], x[4], x[8],  x[12]);
        CHACHA_QR(x[1], x[5], x[9],  x[13]);
        CHACHA_QR(x[2], x[6], x[10], x[14]);
        CHACHA_QR(x[3], x[7], x[11], x[15]);

        // Diagonal rounds
        CHACHA_QR(x[0], x[5], x[10], x[15]);
        CHACHA_QR(x[1], x[6], x[11], x[12]);
        CHACHA_QR(x[2], x[7], x[8],  x[13]);
        CHACHA_QR(x[3], x[4], x[9],  x[14]);
    }

    for (int i = 0; i < 16; i++) {
        uint32_t val = x[i] + state[i];
        out[i * 4 + 0] = uint8_t(val & 0xFF);
        out[i * 4 + 1] = uint8_t((val >> 8) & 0xFF);
        out[i * 4 + 2] = uint8_t((val >> 16) & 0xFF);
        out[i * 4 + 3] = uint8_t((val >> 24) & 0xFF);
    }
}

static void chacha20Process(const uint8_t* in, uint8_t* out, size_t len,
                            const uint8_t key[32], const uint8_t nonce[12], uint32_t counter = 0) {
    uint32_t state[16];
    // Constants "expand 32-byte k"
    state[0] = 0x61707865;
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;

    // Key (8 words)
    for (int i = 0; i < 8; i++) {
        state[4 + i] = uint32_t(key[i * 4 + 0]) |
                      (uint32_t(key[i * 4 + 1]) << 8) |
                      (uint32_t(key[i * 4 + 2]) << 16) |
                      (uint32_t(key[i * 4 + 3]) << 24);
    }

    // Counter
    state[12] = counter;

    // Nonce (3 words = 12 bytes)
    for (int i = 0; i < 3; i++) {
        state[13 + i] = uint32_t(nonce[i * 4 + 0]) |
                       (uint32_t(nonce[i * 4 + 1]) << 8) |
                       (uint32_t(nonce[i * 4 + 2]) << 16) |
                       (uint32_t(nonce[i * 4 + 3]) << 24);
    }

    uint8_t block[64];
    for (size_t i = 0; i < len; i += 64) {
        chacha20Block(block, state);
        state[12]++; // Increment counter

        size_t blockLen = std::min((size_t)64, len - i);
        for (size_t j = 0; j < blockLen; j++) {
            out[i + j] = in[i + j] ^ block[j];
        }
    }
}

// =========================================================================
// 3. Dual-Layer Cascade Cipher: ChaCha20 + AES-256 CTR
// =========================================================================

void cascadeEncrypt(const uint8_t* in, uint8_t* out, size_t len, const uint8_t keyDescriptor[92]) {
    const uint8_t* aesKey = &keyDescriptor[0];       // 32 bytes
    const uint8_t* aesIv = &keyDescriptor[32];       // 16 bytes
    const uint8_t* chachaKey = &keyDescriptor[48];   // 32 bytes
    const uint8_t* chachaNonce = &keyDescriptor[80]; // 12 bytes

    // Layer 1: AES-256 CTR
    aes256CtrProcess(in, out, len, aesKey, aesIv);

    // Layer 2: ChaCha20
    chacha20Process(out, out, len, chachaKey, chachaNonce, 0);
}

void cascadeDecrypt(const uint8_t* in, uint8_t* out, size_t len, const uint8_t keyDescriptor[92]) {
    const uint8_t* aesKey = &keyDescriptor[0];       // 32 bytes
    const uint8_t* aesIv = &keyDescriptor[32];       // 16 bytes
    const uint8_t* chachaKey = &keyDescriptor[48];   // 32 bytes
    const uint8_t* chachaNonce = &keyDescriptor[80]; // 12 bytes

    // Layer 1: ChaCha20 (invert Layer 2)
    chacha20Process(in, out, len, chachaKey, chachaNonce, 0);

    // Layer 2: AES-256 CTR (invert Layer 1)
    aes256CtrProcess(out, out, len, aesKey, aesIv);
}

} // namespace SecureCrypto
