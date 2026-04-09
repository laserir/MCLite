// Stub mbedtls sha256 for native tests
#pragma once
#include <cstdint>
#include <cstddef>

inline int mbedtls_sha256(const uint8_t*, size_t, uint8_t* output, int) {
    // Fill with deterministic but fake hash
    for (int i = 0; i < 32; i++) output[i] = (uint8_t)i;
    return 0;
}
