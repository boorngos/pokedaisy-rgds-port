#include "pd_sha1.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct sha1 {
    uint32_t h[5];
    unsigned char block[64];
    uint64_t bits;
    size_t n;
};

static uint32_t rol(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

static void absorb(struct sha1* s) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t) s->block[i * 4] << 24) | ((uint32_t) s->block[i * 4 + 1] << 16) |
               ((uint32_t) s->block[i * 4 + 2] << 8) | s->block[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

bool pd_sha1_file(const char* path, char out[41]) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    struct sha1 s = { { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u }, { 0 }, 0, 0 };
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            s.block[s.n++] = buf[i];
            if (s.n == 64) {
                absorb(&s);
                s.bits += 512;
                s.n = 0;
            }
        }
    }
    if (ferror(f)) { fclose(f); return false; }
    fclose(f);
    s.bits += (uint64_t) s.n * 8;
    s.block[s.n++] = 0x80;
    if (s.n > 56) {
        while (s.n < 64) s.block[s.n++] = 0;
        absorb(&s);
        s.n = 0;
    }
    while (s.n < 56) s.block[s.n++] = 0;
    for (int i = 7; i >= 0; i--) s.block[s.n++] = (unsigned char) (s.bits >> (i * 8));
    absorb(&s);
    for (int i = 0; i < 5; i++) sprintf(out + i * 8, "%08x", s.h[i]);
    out[40] = 0;
    return true;
}
