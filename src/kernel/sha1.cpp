#include "kernel/sha1.h"

#include <cstring>

namespace swrots::kernel {

static inline uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void Transform(uint32_t state[5], const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = uint32_t(block[i * 4]) << 24 | uint32_t(block[i * 4 + 1]) << 16 | uint32_t(block[i * 4 + 2]) << 8 | block[i * 4 + 3];
    for (int i = 16; i < 80; ++i)
        w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = Rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = Rol(b, 30); b = a; a = t;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void Sha1Init(Sha1Context* c)
{
    c->state[0] = 0x67452301;
    c->state[1] = 0xEFCDAB89;
    c->state[2] = 0x98BADCFE;
    c->state[3] = 0x10325476;
    c->state[4] = 0xC3D2E1F0;
    c->count = 0;
}

void Sha1Update(Sha1Context* c, const uint8_t* data, uint32_t len)
{
    uint32_t used = uint32_t(c->count & 63);
    c->count += len;
    if (used) {
        uint32_t take = 64 - used < len ? 64 - used : len;
        std::memcpy(c->buffer + used, data, take);
        data += take;
        len -= take;
        if (used + take < 64)
            return;
        Transform(c->state, c->buffer);
    }
    for (; len >= 64; data += 64, len -= 64)
        Transform(c->state, data);
    std::memcpy(c->buffer, data, len);
}

void Sha1Final(Sha1Context* c, uint8_t digest[20])
{
    uint64_t bits = c->count * 8;
    uint8_t pad = 0x80;
    Sha1Update(c, &pad, 1);
    uint8_t zero = 0;
    while ((c->count & 63) != 56)
        Sha1Update(c, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; ++i)
        len[i] = uint8_t(bits >> (56 - 8 * i));
    Sha1Update(c, len, 8);
    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = uint8_t(c->state[i] >> 24);
        digest[i * 4 + 1] = uint8_t(c->state[i] >> 16);
        digest[i * 4 + 2] = uint8_t(c->state[i] >> 8);
        digest[i * 4 + 3] = uint8_t(c->state[i]);
    }
}

} // namespace swrots::kernel
