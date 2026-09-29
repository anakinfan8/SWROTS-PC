#pragma once
#include <cstdint>

namespace swrots::kernel {

// Must fit inside the Xbox's opaque SHA context buffer (A_SHA_CTX, 116 bytes).
struct Sha1Context {
    uint32_t state[5];
    uint64_t count; // bytes
    uint8_t buffer[64];
};
static_assert(sizeof(Sha1Context) <= 116);

void Sha1Init(Sha1Context* c);
void Sha1Update(Sha1Context* c, const uint8_t* data, uint32_t len);
void Sha1Final(Sha1Context* c, uint8_t digest[20]);

} // namespace swrots::kernel
