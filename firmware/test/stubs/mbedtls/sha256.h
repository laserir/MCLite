// Stub mbedtls sha256 for native tests
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

inline int mbedtls_sha256(const uint8_t*, size_t, uint8_t* output, int) {
    // Fill with deterministic but fake hash
    for (int i = 0; i < 32; i++) output[i] = (uint8_t)i;
    return 0;
}

// Streaming API (mbedTLS 3.x) — produces the same deterministic hash
struct mbedtls_sha256_context { int dummy; };

inline void mbedtls_sha256_init(mbedtls_sha256_context* ctx) { ctx->dummy = 0; }
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int  mbedtls_sha256_starts(mbedtls_sha256_context*, int) { return 0; }
inline int  mbedtls_sha256_update(mbedtls_sha256_context*, const uint8_t*, size_t) { return 0; }
inline int  mbedtls_sha256_finish(mbedtls_sha256_context*, uint8_t* output) {
    for (int i = 0; i < 32; i++) output[i] = (uint8_t)i;
    return 0;
}
