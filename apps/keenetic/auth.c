// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GrakovNe
#include "auth.h"
#include "app_text.h"
#define keenetic_clear(p, n) __builtin_memset(p, 0, n)
#include <stddef.h>

static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static const uint32_t md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
static const uint32_t sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
typedef struct {
    uint32_t h[8];
    uint8_t block[64];
    uint64_t bytes;
    unsigned used;
    bool sha;
} hash_t;
static void compress(hash_t *s)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        const uint8_t *b = s->block + i * 4;
        uint32_t value = (uint32_t)b[3] << 24 | (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8 | b[0];
        w[i] = s->sha ? __builtin_bswap32(value) : value;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    if (!s->sha) {
        /* The fixed MD5 schedule saves code on the device interpreter. */
        static const uint8_t indices[64] = {
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
            1, 6, 11, 0, 5, 10, 15, 4, 9, 14, 3, 8, 13, 2, 7, 12,
            5, 8, 11, 14, 1, 4, 7, 10, 13, 0, 3, 6, 9, 12, 15, 2,
            0, 7, 14, 5, 12, 3, 10, 1, 8, 15, 6, 13, 4, 11, 2, 9,
        };
        static const uint8_t shifts[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
        };
        for (unsigned i = 0; i < 64; i++) {
            unsigned round = i / 16;
            uint32_t f;
            if (round == 0) {
                f = (b & c) | (~b & d);
            } else if (round == 1) {
                f = (d & b) | (~d & c);
            } else if (round == 2) {
                f = b ^ c ^ d;
            } else {
                f = c ^ (b | ~d);
            }
            uint32_t next = b + rotate(a + f + md5_k[i] + w[indices[i]], 32 - shifts[i]);
            a = d;
            d = c;
            c = b;
            b = next;
        }
    } else {
        for (int i = 16; i < 64; i++) {
            uint32_t x = w[i - 15], y = w[i - 2];
            w[i] = w[i - 16] + (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + w[i - 7] +
                   (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
        }
        uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) +
                          ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
            uint32_t t2 =
                (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        s->h[4] += e;
        s->h[5] += f;
        s->h[6] += g;
        s->h[7] += h;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
}
static void begin(hash_t *s, bool sha)
{
    keenetic_clear(s, sizeof *s);
    s->sha = sha;
    static const uint32_t initial[2][8] = {
        {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476,
         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19},
        {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}
    };
    for (int i = 0; i < 8; i++)
        s->h[i] = initial[sha][i];
}
static void byte(hash_t *s, uint8_t value)
{
    s->block[s->used++] = value;
    s->bytes++;
    if (s->used == 64) {
        compress(s);
        s->used = 0;
    }
}
static void add(hash_t *s, const char *text)
{
    while (*text)
        byte(s, (uint8_t)*text++);
}
static void finish(hash_t *s, char *out)
{
    uint64_t bits = s->bytes * 8;
    byte(s, 0x80);
    while (s->used != 56)
        byte(s, 0);
    for (int i = 0; i < 8; i++)
        byte(s, (uint8_t)(bits >> (s->sha ? (7 - i) * 8 : i * 8)));
    static const char hex[] = "0123456789abcdef";
    int count = s->sha ? 64 : 32;
    for (int i = 0; i < count; i++) {
        int nibble = s->sha ? 7 - i % 8 : (i % 8) ^ 1;
        out[i] = hex[(s->h[i / 8] >> (nibble * 4)) & 15];
    }
    out[count] = 0;
}
void keenetic_digest(const char *user, const char *realm, const char *password,
                     const char *challenge, char out[65])
{
    hash_t h;
    begin(&h, false);
    add(&h, user);
    add(&h, ":");
    add(&h, realm);
    add(&h, ":");
    add(&h, password);
    char md5[33];
    finish(&h, md5);
    begin(&h, true);
    add(&h, challenge);
    add(&h, md5);
    finish(&h, out);
}
bool keenetic_cookie(const char *header, char out[256])
{
    unsigned n = 0;
    bool equal = false;
    while (header[n] && header[n] != ';') {
        unsigned char c = (unsigned char)header[n];
        if (n == 255 || c <= 32 || c >= 127 || c == ',' || c == '{' || c == '}')
            return false;
        if (c == '=')
            equal = true;
        out[n] = header[n];
        n++;
    }
    out[n] = 0;
    return n > 2 && equal && out[0] != '=';
}
bool keenetic_login_body(const char *user, const char digest[65], char out[513])
{
    app_text_copy(out, 513, "{\"login\":\"");
    int n = 10;
    for (; *user; user++) {
        unsigned char c = (unsigned char)*user;
        int escaped = c == '"' || c == '\\';
        if (c < 32 || n + 1 + escaped > 432) return false;
        if (escaped) out[n++] = '\\';
        out[n++] = (char)c;
    }
    out[n] = 0;
    app_text_t t = {out + n, out + 512};
    app_text_add(&t, "\",\"password\":\"");
    app_text_add(&t, digest);
    app_text_add(&t, "\"}");
    return true;
}
