#pragma once
#include <cstdint>
#include <cstddef>

namespace SecureCrypto {
    // ChaCha20 + AES-256 CTR Dual-Layer Cascade Decryption
    void cascadeDecrypt(const uint8_t* in, uint8_t* out, size_t len, const uint8_t keyDescriptor[92]);

    // ChaCha20 + AES-256 CTR Dual-Layer Cascade Encryption
    void cascadeEncrypt(const uint8_t* in, uint8_t* out, size_t len, const uint8_t keyDescriptor[92]);
}
