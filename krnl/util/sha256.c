#include "sha256.h"
#include "cstdlib.h"

#define ROTR(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))
#define SHR(x,  n)  ((x) >> (n))

#define CH(x, y, z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define S0(x)  (ROTR(x,  2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x)  (ROTR(x,  6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define G0(x)  (ROTR(x,  7) ^ ROTR(x, 18) ^ SHR (x,  3))
#define G1(x)  (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR (x, 10))

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void process_block(const uint8_t block[64], uint32_t h[8]) {
    uint32_t w[64];

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4 + 0] << 24)
             | ((uint32_t)block[i*4 + 1] << 16)
             | ((uint32_t)block[i*4 + 2] <<  8)
             | ((uint32_t)block[i*4 + 3]);
    }
    for (int i = 16; i < 64; i++)
        w[i] = G1(w[i-2]) + w[i-7] + G0(w[i-15]) + w[i-16];

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + S1(e) + CH(e,f,g) + K[i] + w[i];
        uint32_t t2 = S0(a) + MAJ(a,b,c);
        hh = g; g = f; f = e; e = d + t1;
        d  = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256(const void* data, size_t len, sha256_digest_t* out) {
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    const uint8_t* src     = (const uint8_t*)data;
    size_t         full    = len / 64;
    size_t         partial = len % 64;

    for (size_t i = 0; i < full; i++)
        process_block(src + i * 64, h);

    uint8_t pad[128];
    memset(pad, 0, sizeof(pad));
    memcpy(pad, src + full * 64, partial);
    pad[partial] = 0x80;

    size_t pad_blocks = (partial < 56) ? 1 : 2;

    uint64_t bit_len = (uint64_t)len * 8;
    uint8_t* len_pos = pad + pad_blocks * 64 - 8;
    len_pos[0] = (uint8_t)(bit_len >> 56);
    len_pos[1] = (uint8_t)(bit_len >> 48);
    len_pos[2] = (uint8_t)(bit_len >> 40);
    len_pos[3] = (uint8_t)(bit_len >> 32);
    len_pos[4] = (uint8_t)(bit_len >> 24);
    len_pos[5] = (uint8_t)(bit_len >> 16);
    len_pos[6] = (uint8_t)(bit_len >>  8);
    len_pos[7] = (uint8_t)(bit_len);

    for (size_t i = 0; i < pad_blocks; i++)
        process_block(pad + i * 64, h);

    for (int i = 0; i < 8; i++) {
        out->bytes[i*4 + 0] = (uint8_t)(h[i] >> 24);
        out->bytes[i*4 + 1] = (uint8_t)(h[i] >> 16);
        out->bytes[i*4 + 2] = (uint8_t)(h[i] >>  8);
        out->bytes[i*4 + 3] = (uint8_t)(h[i]);
    }
}

void sha256_to_hex(const sha256_digest_t* digest, char* buf) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        buf[i*2 + 0] = hex[digest->bytes[i] >> 4];
        buf[i*2 + 1] = hex[digest->bytes[i] & 0xf];
    }
    buf[64] = '\0';
}