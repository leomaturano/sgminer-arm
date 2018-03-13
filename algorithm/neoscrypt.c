/*
 * Copyright (c) 2009 Colin Percival, 2011 ArtForz
 * Copyright (c) 2012 Andrew Moon (floodyberry)
 * Copyright (c) 2012 Samuel Neves <sneves@dei.uc.pt>
 * Copyright (c) 2014 John Doering <ghostlander@phoenixcoin.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"
#include "miner.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "neoscrypt.h"

#define SCRYPT_BLOCK_SIZE 64
#define SCRYPT_HASH_BLOCK_SIZE 64
#define SCRYPT_HASH_DIGEST_SIZE 32

typedef uint8_t hash_digest[SCRYPT_HASH_DIGEST_SIZE];

#define ROTL32(a,b) (((a) << (b)) | ((a) >> (32 - b)))
#define ROTR32(a,b) (((a) >> (b)) | ((a) << (32 - b)))

#define U8TO32_BE(p) \
    (((uint32_t)((p)[0]) << 24) | ((uint32_t)((p)[1]) << 16) | \
    ((uint32_t)((p)[2]) <<  8) | ((uint32_t)((p)[3])))

#define U32TO8_BE(p, v) \
    (p)[0] = (uint8_t)((v) >> 24); (p)[1] = (uint8_t)((v) >> 16); \
    (p)[2] = (uint8_t)((v) >>  8); (p)[3] = (uint8_t)((v)      );

#define U64TO8_BE(p, v) \
    U32TO8_BE((p),     (uint32_t)((v) >> 32)); \
    U32TO8_BE((p) + 4, (uint32_t)((v)      ));

#if (WINDOWS)
/* sizeof(unsigned long) = 4 for MinGW64 */
typedef unsigned long long ulong;
#else
typedef unsigned long ulong;
#endif

typedef unsigned int  uint;
typedef unsigned char uchar;
typedef unsigned int  ubool;


//#define MIN(a, b) ((a) < (b) ? a : b)
//#define MAX(a, b) ((a) > (b) ? a : b)


#if (SHA256)

/* SHA-256 */

static const uint32_t sha256_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define Ch(x,y,z)  (z ^ (x & (y ^ z)))
#define Maj(x,y,z) (((x | y) & z) | (x & y))
#define S0(x)      (ROTR32(x,  2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define S1(x)      (ROTR32(x,  6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define G0(x)      (ROTR32(x,  7) ^ ROTR32(x, 18) ^ (x >>  3))
#define G1(x)      (ROTR32(x, 17) ^ ROTR32(x, 19) ^ (x >> 10))
#define W0(in,i)   (U8TO32_BE(&in[i * 4]))
#define W1(i)      (G1(w[i - 2]) + w[i - 7] + G0(w[i - 15]) + w[i - 16])
#define STEP(i) \
    t1 = S0(r[0]) + Maj(r[0], r[1], r[2]); \
    t0 = r[7] + S1(r[4]) + Ch(r[4], r[5], r[6]) + sha256_constants[i] + w[i]; \
    r[7] = r[6]; \
    r[6] = r[5]; \
    r[5] = r[4]; \
    r[4] = r[3] + t0; \
    r[3] = r[2]; \
    r[2] = r[1]; \
    r[1] = r[0]; \
    r[0] = t0 + t1;


typedef struct sha256_hash_state_t {
    uint32_t H[8];
    uint64_t T;
    uint32_t leftover;
    uint8_t buffer[SCRYPT_HASH_BLOCK_SIZE];
} sha256_hash_state;


static void sha256_blocks(sha256_hash_state *S, const uint8_t *in, size_t blocks) {
    uint32_t r[8], w[64], t0, t1;
    size_t i;

    for(i = 0; i < 8; i++)
      r[i] = S->H[i];

    while(blocks--) {
        for(i =  0; i < 16; i++) {
            w[i] = W0(in, i);
        }
        for(i = 16; i < 64; i++) {
            w[i] = W1(i);
        }
        for(i =  0; i < 64; i++) {
            STEP(i);
        }
        for(i =  0; i <  8; i++) {
            r[i] += S->H[i];
            S->H[i] = r[i];
        }
        S->T += SCRYPT_HASH_BLOCK_SIZE * 8;
        in += SCRYPT_HASH_BLOCK_SIZE;
    }
}

static void neoscrypt_hash_init_sha256(sha256_hash_state *S) {
    S->H[0] = 0x6a09e667;
    S->H[1] = 0xbb67ae85;
    S->H[2] = 0x3c6ef372;
    S->H[3] = 0xa54ff53a;
    S->H[4] = 0x510e527f;
    S->H[5] = 0x9b05688c;
    S->H[6] = 0x1f83d9ab;
    S->H[7] = 0x5be0cd19;
    S->T = 0;
    S->leftover = 0;
}

static void neoscrypt_hash_update_sha256(sha256_hash_state *S, const uint8_t *in, size_t inlen) {
    size_t blocks, want;

    /* handle the previous data */
    if(S->leftover) {
        want = (SCRYPT_HASH_BLOCK_SIZE - S->leftover);
        want = (want < inlen) ? want : inlen;
        memcpy(S->buffer + S->leftover, in, want);
        S->leftover += (uint32_t)want;
        if(S->leftover < SCRYPT_HASH_BLOCK_SIZE)
          return;
        in += want;
        inlen -= want;
        sha256_blocks(S, S->buffer, 1);
    }

    /* handle the current data */
    blocks = (inlen & ~(SCRYPT_HASH_BLOCK_SIZE - 1));
    S->leftover = (uint32_t)(inlen - blocks);
    if(blocks) {
        sha256_blocks(S, in, blocks / SCRYPT_HASH_BLOCK_SIZE);
        in += blocks;
    }

    /* handle leftover data */
    if(S->leftover)
      memcpy(S->buffer, in, S->leftover);
}

static void neoscrypt_hash_finish_sha256(sha256_hash_state *S, uint8_t *hash) {
    uint64_t t = S->T + (S->leftover * 8);

    S->buffer[S->leftover] = 0x80;
    if(S->leftover <= 55) {
        memset(S->buffer + S->leftover + 1, 0, 55 - S->leftover);
    } else {
        memset(S->buffer + S->leftover + 1, 0, 63 - S->leftover);
        sha256_blocks(S, S->buffer, 1);
        memset(S->buffer, 0, 56);
    }

    U64TO8_BE(S->buffer + 56, t);
    sha256_blocks(S, S->buffer, 1);

    U32TO8_BE(&hash[ 0], S->H[0]);
    U32TO8_BE(&hash[ 4], S->H[1]);
    U32TO8_BE(&hash[ 8], S->H[2]);
    U32TO8_BE(&hash[12], S->H[3]);
    U32TO8_BE(&hash[16], S->H[4]);
    U32TO8_BE(&hash[20], S->H[5]);
    U32TO8_BE(&hash[24], S->H[6]);
    U32TO8_BE(&hash[28], S->H[7]);
}

static void neoscrypt_hash_sha256(hash_digest hash, const uint8_t *m, size_t mlen) {
    sha256_hash_state st;
    neoscrypt_hash_init_sha256(&st);
    neoscrypt_hash_update_sha256(&st, m, mlen);
    neoscrypt_hash_finish_sha256(&st, hash);
}


/* HMAC for SHA-256 */

typedef struct sha256_hmac_state_t {
    sha256_hash_state inner, outer;
} sha256_hmac_state;

static void neoscrypt_hmac_init_sha256(sha256_hmac_state *st, const uint8_t *key, size_t keylen) {
    uint8_t pad[SCRYPT_HASH_BLOCK_SIZE] = {0};
    size_t i;

    neoscrypt_hash_init_sha256(&st->inner);
    neoscrypt_hash_init_sha256(&st->outer);

    if(keylen <= SCRYPT_HASH_BLOCK_SIZE) {
        /* use the key directly if it's <= blocksize bytes */
        memcpy(pad, key, keylen);
    } else {
        /* if it's > blocksize bytes, hash it */
        neoscrypt_hash_sha256(pad, key, keylen);
    }

    /* inner = (key ^ 0x36) */
    /* h(inner || ...) */
    for(i = 0; i < SCRYPT_HASH_BLOCK_SIZE; i++)
      pad[i] ^= 0x36;
    neoscrypt_hash_update_sha256(&st->inner, pad, SCRYPT_HASH_BLOCK_SIZE);

    /* outer = (key ^ 0x5c) */
    /* h(outer || ...) */
    for(i = 0; i < SCRYPT_HASH_BLOCK_SIZE; i++)
      pad[i] ^= (0x5c ^ 0x36);
    neoscrypt_hash_update_sha256(&st->outer, pad, SCRYPT_HASH_BLOCK_SIZE);
}

static void neoscrypt_hmac_update_sha256(sha256_hmac_state *st, const uint8_t *m, size_t mlen) {
    /* h(inner || m...) */
    neoscrypt_hash_update_sha256(&st->inner, m, mlen);
}

static void neoscrypt_hmac_finish_sha256(sha256_hmac_state *st, hash_digest mac) {
    /* h(inner || m) */
    hash_digest innerhash;
    neoscrypt_hash_finish_sha256(&st->inner, innerhash);

    /* h(outer || h(inner || m)) */
    neoscrypt_hash_update_sha256(&st->outer, innerhash, sizeof(innerhash));
    neoscrypt_hash_finish_sha256(&st->outer, mac);
}


/* PBKDF2 for SHA-256 */

static void neoscrypt_pbkdf2_sha256(const uint8_t *password, size_t password_len,
  const uint8_t *salt, size_t salt_len, uint64_t N, uint8_t *output, size_t output_len) {
    sha256_hmac_state hmac_pw, hmac_pw_salt, work;
    hash_digest ti, u;
    uint8_t be[4];
    uint32_t i, j, k, blocks;

    /* bytes must be <= (0xffffffff - (SCRYPT_HASH_DIGEST_SIZE - 1)), which they will always be under scrypt */

    /* hmac(password, ...) */
    neoscrypt_hmac_init_sha256(&hmac_pw, password, password_len);

    /* hmac(password, salt...) */
    hmac_pw_salt = hmac_pw;
    neoscrypt_hmac_update_sha256(&hmac_pw_salt, salt, salt_len);

    blocks = ((uint32_t)output_len + (SCRYPT_HASH_DIGEST_SIZE - 1)) / SCRYPT_HASH_DIGEST_SIZE;
    for(i = 1; i <= blocks; i++) {
        /* U1 = hmac(password, salt || be(i)) */
        U32TO8_BE(be, i);
        work = hmac_pw_salt;
        neoscrypt_hmac_update_sha256(&work, be, 4);
        neoscrypt_hmac_finish_sha256(&work, ti);
        memcpy(u, ti, sizeof(u));

        /* T[i] = U1 ^ U2 ^ U3... */
        for(j = 0; j < N - 1; j++) {
            /* UX = hmac(password, U{X-1}) */
            work = hmac_pw;
            neoscrypt_hmac_update_sha256(&work, u, SCRYPT_HASH_DIGEST_SIZE);
            neoscrypt_hmac_finish_sha256(&work, u);

            /* T[i] ^= UX */
            for(k = 0; k < sizeof(u); k++)
              ti[k] ^= u[k];
        }

        memcpy(output, ti, (output_len > SCRYPT_HASH_DIGEST_SIZE) ? SCRYPT_HASH_DIGEST_SIZE : output_len);
        output += SCRYPT_HASH_DIGEST_SIZE;
        output_len -= SCRYPT_HASH_DIGEST_SIZE;
    }
}

#endif


#if (BLAKE256)

/* BLAKE-256 */

const uint8_t blake256_sigma[] = {
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
    14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3,
    11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4,
     7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8,
     9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13,
     2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9,
    12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11,
    13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10,
     6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5,
    10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13 ,0,
};

const uint32_t blake256_constants[16] = {
    0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344,0xa4093822, 0x299f31d0, 0x082efa98, 0xec4e6c89,
    0x452821e6, 0x38d01377, 0xbe5466cf, 0x34e90c6c,0xc0ac29b7, 0xc97c50dd, 0x3f84d5b5, 0xb5470917
};

typedef struct blake256_hash_state_t {
    uint32_t H[8], T[2];
    uint32_t leftover;
    uint8_t buffer[SCRYPT_HASH_BLOCK_SIZE];
} blake256_hash_state;

static void blake256_blocks(blake256_hash_state *S, const uint8_t *in, size_t blocks) {
    const uint8_t *sigma, *sigma_end = blake256_sigma + (10 * 16);
    uint32_t m[16], v[16], h[8], t[2];
    uint32_t i;

    for(i = 0; i < 8; i++)
      h[i] = S->H[i];
    for(i = 0; i < 2; i++)
      t[i] = S->T[i];

    while(blocks--) {
        t[0] += 512;
        t[1] += (t[0] < 512) ? 1 : 0;

        for(i = 0; i <  8; i++)
          v[i] = h[i];
        for(i = 0; i <  4; i++)
          v[i + 8] = blake256_constants[i];
        for(i = 0; i <  2; i++)
          v[i + 12] = blake256_constants[i+4] ^ t[0];
        for(i = 0; i <  2; i++)
          v[i + 14] = blake256_constants[i+6] ^ t[1];

        for(i = 0; i < 16; i++)
          m[i] = U8TO32_BE(&in[i * 4]);

        in += 64;

#define G(a,b,c,d,e) \
    v[a] += (m[sigma[e+0]] ^ blake256_constants[sigma[e+1]]) + v[b]; \
    v[d] = ROTR32(v[d] ^ v[a],16); \
    v[c] += v[d]; \
    v[b] = ROTR32(v[b] ^ v[c],12); \
    v[a] += (m[sigma[e+1]] ^ blake256_constants[sigma[e+0]]) + v[b]; \
    v[d] = ROTR32(v[d] ^ v[a], 8); \
    v[c] += v[d]; \
    v[b] = ROTR32(v[b] ^ v[c], 7);

        for(i = 0, sigma = blake256_sigma; i < 14; i++) {
            G(0, 4, 8,12, 0);
            G(1, 5, 9,13, 2);
            G(2, 6,10,14, 4);
            G(3, 7,11,15, 6);

            G(0, 5,10,15, 8);
            G(1, 6,11,12,10);
            G(2, 7, 8,13,12);
            G(3, 4, 9,14,14);

            sigma += 16;
            if(sigma == sigma_end)
              sigma = blake256_sigma;
        }

#undef G

        for(i = 0; i < 8; i++)
          h[i] ^= (v[i] ^ v[i + 8]);
    }

    for(i = 0; i < 8; i++)
      S->H[i] = h[i];
    for(i = 0; i < 2; i++)
      S->T[i] = t[i];
}

static void neoscrypt_hash_init_blake256(blake256_hash_state *S) {
    S->H[0] = 0x6a09e667ULL;
    S->H[1] = 0xbb67ae85ULL;
    S->H[2] = 0x3c6ef372ULL;
    S->H[3] = 0xa54ff53aULL;
    S->H[4] = 0x510e527fULL;
    S->H[5] = 0x9b05688cULL;
    S->H[6] = 0x1f83d9abULL;
    S->H[7] = 0x5be0cd19ULL;
    S->T[0] = 0;
    S->T[1] = 0;
    S->leftover = 0;
}

static void neoscrypt_hash_update_blake256(blake256_hash_state *S, const uint8_t *in, size_t inlen) {
    size_t blocks, want;

    /* handle the previous data */
    if(S->leftover) {
        want = (SCRYPT_HASH_BLOCK_SIZE - S->leftover);
        want = (want < inlen) ? want : inlen;
        memcpy(S->buffer + S->leftover, in, want);
        S->leftover += (uint32_t)want;
        if(S->leftover < SCRYPT_HASH_BLOCK_SIZE)
          return;
        in += want;
        inlen -= want;
        blake256_blocks(S, S->buffer, 1);
    }

    /* handle the current data */
    blocks = (inlen & ~(SCRYPT_HASH_BLOCK_SIZE - 1));
    S->leftover = (uint32_t)(inlen - blocks);
    if(blocks) {
        blake256_blocks(S, in, blocks / SCRYPT_HASH_BLOCK_SIZE);
        in += blocks;
    }

    /* handle leftover data */
    if(S->leftover)
      memcpy(S->buffer, in, S->leftover);
}

static void neoscrypt_hash_finish_blake256(blake256_hash_state *S, uint8_t *hash) {
    uint32_t th, tl, bits;

    bits = (S->leftover << 3);
    tl = S->T[0] + bits;
    th = S->T[1];
    if(S->leftover == 0) {
        S->T[0] = (uint32_t)0 - (uint32_t)512;
        S->T[1] = (uint32_t)0 - (uint32_t)1;
    } else if(S->T[0] == 0) {
        S->T[0] = ((uint32_t)0 - (uint32_t)512) + bits;
        S->T[1] = S->T[1] - 1;
    } else {
        S->T[0] -= (512 - bits);
    }

    S->buffer[S->leftover] = 0x80;
    if(S->leftover <= 55) {
        memset(S->buffer + S->leftover + 1, 0, 55 - S->leftover);
    } else {
        memset(S->buffer + S->leftover + 1, 0, 63 - S->leftover);
        blake256_blocks(S, S->buffer, 1);
        S->T[0] = (uint32_t)0 - (uint32_t)512;
        S->T[1] = (uint32_t)0 - (uint32_t)1;
        memset(S->buffer, 0, 56);
    }
    S->buffer[55] |= 1;
    U32TO8_BE(S->buffer + 56, th);
    U32TO8_BE(S->buffer + 60, tl);
    blake256_blocks(S, S->buffer, 1);

    U32TO8_BE(&hash[ 0], S->H[0]);
    U32TO8_BE(&hash[ 4], S->H[1]);
    U32TO8_BE(&hash[ 8], S->H[2]);
    U32TO8_BE(&hash[12], S->H[3]);
    U32TO8_BE(&hash[16], S->H[4]);
    U32TO8_BE(&hash[20], S->H[5]);
    U32TO8_BE(&hash[24], S->H[6]);
    U32TO8_BE(&hash[28], S->H[7]);
}

static void neoscrypt_hash_blake256(hash_digest hash, const uint8_t *m, size_t mlen) {
    blake256_hash_state st;
    neoscrypt_hash_init_blake256(&st);
    neoscrypt_hash_update_blake256(&st, m, mlen);
    neoscrypt_hash_finish_blake256(&st, hash);
}


/* HMAC for BLAKE-256 */

typedef struct blake256_hmac_state_t {
    blake256_hash_state inner, outer;
} blake256_hmac_state;

static void neoscrypt_hmac_init_blake256(blake256_hmac_state *st, const uint8_t *key, size_t keylen) {
    uint8_t pad[SCRYPT_HASH_BLOCK_SIZE] = {0};
    size_t i;

    neoscrypt_hash_init_blake256(&st->inner);
    neoscrypt_hash_init_blake256(&st->outer);

    if(keylen <= SCRYPT_HASH_BLOCK_SIZE) {
        /* use the key directly if it's <= blocksize bytes */
        memcpy(pad, key, keylen);
    } else {
        /* if it's > blocksize bytes, hash it */
        neoscrypt_hash_blake256(pad, key, keylen);
    }

    /* inner = (key ^ 0x36) */
    /* h(inner || ...) */
    for(i = 0; i < SCRYPT_HASH_BLOCK_SIZE; i++)
      pad[i] ^= 0x36;
    neoscrypt_hash_update_blake256(&st->inner, pad, SCRYPT_HASH_BLOCK_SIZE);

    /* outer = (key ^ 0x5c) */
    /* h(outer || ...) */
    for(i = 0; i < SCRYPT_HASH_BLOCK_SIZE; i++)
      pad[i] ^= (0x5c ^ 0x36);
    neoscrypt_hash_update_blake256(&st->outer, pad, SCRYPT_HASH_BLOCK_SIZE);
}

static void neoscrypt_hmac_update_blake256(blake256_hmac_state *st, const uint8_t *m, size_t mlen) {
    /* h(inner || m...) */
    neoscrypt_hash_update_blake256(&st->inner, m, mlen);
}

static void neoscrypt_hmac_finish_blake256(blake256_hmac_state *st, hash_digest mac) {
    /* h(inner || m) */
    hash_digest innerhash;
    neoscrypt_hash_finish_blake256(&st->inner, innerhash);

    /* h(outer || h(inner || m)) */
    neoscrypt_hash_update_blake256(&st->outer, innerhash, sizeof(innerhash));
    neoscrypt_hash_finish_blake256(&st->outer, mac);
}


/* PBKDF2 for BLAKE-256 */

static void neoscrypt_pbkdf2_blake256(const uint8_t *password, size_t password_len,
  const uint8_t *salt, size_t salt_len, uint64_t N, uint8_t *output, size_t output_len) {
    blake256_hmac_state hmac_pw, hmac_pw_salt, work;
    hash_digest ti, u;
    uint8_t be[4];
    uint32_t i, j, k, blocks;

    /* bytes must be <= (0xffffffff - (SCRYPT_HASH_DIGEST_SIZE - 1)), which they will always be under scrypt */

    /* hmac(password, ...) */
    neoscrypt_hmac_init_blake256(&hmac_pw, password, password_len);

    /* hmac(password, salt...) */
    hmac_pw_salt = hmac_pw;
    neoscrypt_hmac_update_blake256(&hmac_pw_salt, salt, salt_len);

    blocks = ((uint32_t)output_len + (SCRYPT_HASH_DIGEST_SIZE - 1)) / SCRYPT_HASH_DIGEST_SIZE;
    for(i = 1; i <= blocks; i++) {
        /* U1 = hmac(password, salt || be(i)) */
        U32TO8_BE(be, i);
        work = hmac_pw_salt;
        neoscrypt_hmac_update_blake256(&work, be, 4);
        neoscrypt_hmac_finish_blake256(&work, ti);
        memcpy(u, ti, sizeof(u));

        /* T[i] = U1 ^ U2 ^ U3... */
        for(j = 0; j < N - 1; j++) {
            /* UX = hmac(password, U{X-1}) */
            work = hmac_pw;
            neoscrypt_hmac_update_blake256(&work, u, SCRYPT_HASH_DIGEST_SIZE);
            neoscrypt_hmac_finish_blake256(&work, u);

            /* T[i] ^= UX */
            for(k = 0; k < sizeof(u); k++)
              ti[k] ^= u[k];
        }

        memcpy(output, ti, (output_len > SCRYPT_HASH_DIGEST_SIZE) ? SCRYPT_HASH_DIGEST_SIZE : output_len);
        output += SCRYPT_HASH_DIGEST_SIZE;
        output_len -= SCRYPT_HASH_DIGEST_SIZE;
    }
}

#endif


/* NeoScrypt */

/* Salsa20, rounds must be a multiple of 2 */
static void neoscrypt_salsa(uint *X, uint rounds) {
    uint x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, t;

    x0 = X[0];   x1 = X[1];   x2 = X[2];   x3 = X[3];
    x4 = X[4];   x5 = X[5];   x6 = X[6];   x7 = X[7];
    x8 = X[8];   x9 = X[9];  x10 = X[10]; x11 = X[11];
   x12 = X[12]; x13 = X[13]; x14 = X[14]; x15 = X[15];

#define quarter(a, b, c, d) \
    t = a + d; t = ROTL32(t,  7); b ^= t; \
    t = b + a; t = ROTL32(t,  9); c ^= t; \
    t = c + b; t = ROTL32(t, 13); d ^= t; \
    t = d + c; t = ROTL32(t, 18); a ^= t;

    for(; rounds; rounds -= 2) {
        quarter( x0,  x4,  x8, x12);
        quarter( x5,  x9, x13,  x1);
        quarter(x10, x14,  x2,  x6);
        quarter(x15,  x3,  x7, x11);
        quarter( x0,  x1,  x2,  x3);
        quarter( x5,  x6,  x7,  x4);
        quarter(x10, x11,  x8,  x9);
        quarter(x15, x12, x13, x14);
    }

    X[0] += x0;   X[1] += x1;   X[2] += x2;   X[3] += x3;
    X[4] += x4;   X[5] += x5;   X[6] += x6;   X[7] += x7;
    X[8] += x8;   X[9] += x9;  X[10] += x10; X[11] += x11;
   X[12] += x12; X[13] += x13; X[14] += x14; X[15] += x15;

#undef quarter
}

/* ChaCha20, rounds must be a multiple of 2 */
static void neoscrypt_chacha(uint *X, uint rounds) {
    uint x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, t;

    x0 = X[0];   x1 = X[1];   x2 = X[2];   x3 = X[3];
    x4 = X[4];   x5 = X[5];   x6 = X[6];   x7 = X[7];
    x8 = X[8];   x9 = X[9];  x10 = X[10]; x11 = X[11];
   x12 = X[12]; x13 = X[13]; x14 = X[14]; x15 = X[15];

#define quarter(a,b,c,d) \
    a += b; t = d ^ a; d = ROTL32(t, 16); \
    c += d; t = b ^ c; b = ROTL32(t, 12); \
    a += b; t = d ^ a; d = ROTL32(t,  8); \
    c += d; t = b ^ c; b = ROTL32(t,  7);

    for(; rounds; rounds -= 2) {
        quarter( x0,  x4,  x8, x12);
        quarter( x1,  x5,  x9, x13);
        quarter( x2,  x6, x10, x14);
        quarter( x3,  x7, x11, x15);
        quarter( x0,  x5, x10, x15);
        quarter( x1,  x6, x11, x12);
        quarter( x2,  x7,  x8, x13);
        quarter( x3,  x4,  x9, x14);
    }

    X[0] += x0;   X[1] += x1;   X[2] += x2;   X[3] += x3;
    X[4] += x4;   X[5] += x5;   X[6] += x6;   X[7] += x7;
    X[8] += x8;   X[9] += x9;  X[10] += x10; X[11] += x11;
   X[12] += x12; X[13] += x13; X[14] += x14; X[15] += x15;

#undef quarter
}


/* Fast 32-bit / 64-bit memcpy();
 * len must be a multiple of 32 bytes */
static void neoscrypt_blkcpy(void *dstp, const void *srcp, uint len) {
    ulong *dst = (ulong *) dstp;
    ulong *src = (ulong *) srcp;
    uint i;

    for(i = 0; i < (len / sizeof(ulong)); i += 4) {
        dst[i]     = src[i];
        dst[i + 1] = src[i + 1];
        dst[i + 2] = src[i + 2];
        dst[i + 3] = src[i + 3];
    }
}

/* Fast 32-bit / 64-bit block swapper;
 * len must be a multiple of 32 bytes */
static void neoscrypt_blkswp(void *blkAp, void *blkBp, uint len) {
    ulong *blkA = (ulong *) blkAp;
    ulong *blkB = (ulong *) blkBp;
    register ulong t0, t1, t2, t3;
    uint i;

    for(i = 0; i < (len / sizeof(ulong)); i += 4) {
        t0          = blkA[i];
        t1          = blkA[i + 1];
        t2          = blkA[i + 2];
        t3          = blkA[i + 3];
        blkA[i]     = blkB[i];
        blkA[i + 1] = blkB[i + 1];
        blkA[i + 2] = blkB[i + 2];
        blkA[i + 3] = blkB[i + 3];
        blkB[i]     = t0;
        blkB[i + 1] = t1;
        blkB[i + 2] = t2;
        blkB[i + 3] = t3;
    }
}

/* Fast 32-bit / 64-bit block XOR engine;
 * len must be a multiple of 32 bytes */
static void neoscrypt_blkxor(void *dstp, const void *srcp, uint len) {
    ulong *dst = (ulong *) dstp;
    ulong *src = (ulong *) srcp;
    uint i;

    for(i = 0; i < (len / sizeof(ulong)); i += 4) {
        dst[i]     ^= src[i];
        dst[i + 1] ^= src[i + 1];
        dst[i + 2] ^= src[i + 2];
        dst[i + 3] ^= src[i + 3];
    }
}


/* 32-bit / 64-bit optimised memcpy() */
static void neoscrypt_copy(void *dstp, const void *srcp, uint len) {
    ulong *dst = (ulong *) dstp;
    ulong *src = (ulong *) srcp;
    uint i, tail;

    for(i = 0; i < (len / sizeof(ulong)); i++)
      dst[i] = src[i];

    tail = len & (sizeof(ulong) - 1);
    if(tail) {
        uchar *dstb = (uchar *) dstp;
        uchar *srcb = (uchar *) srcp;

        for(i = len - tail; i < len; i++)
          dstb[i] = srcb[i];
    }
}

/* 32-bit / 64-bit optimised memory erase aka memset() to zero */
static void neoscrypt_erase(void *dstp, uint len) {
    const ulong null = 0;
    ulong *dst = (ulong *) dstp;
    uint i, tail;

    for(i = 0; i < (len / sizeof(ulong)); i++)
      dst[i] = null;

    tail = len & (sizeof(ulong) - 1);
    if(tail) {
        uchar *dstb = (uchar *) dstp;

        for(i = len - tail; i < len; i++)
          dstb[i] = (uchar)null;
    }
}

/* 32-bit / 64-bit optimised XOR engine */
static void neoscrypt_xor(void *dstp, const void *srcp, uint len) {
    ulong *dst = (ulong *) dstp;
    ulong *src = (ulong *) srcp;
    uint i, tail;

    for(i = 0; i < (len / sizeof(ulong)); i++)
      dst[i] ^= src[i];

    tail = len & (sizeof(ulong) - 1);
    if(tail) {
        uchar *dstb = (uchar *) dstp;
        uchar *srcb = (uchar *) srcp;

        for(i = len - tail; i < len; i++)
          dstb[i] ^= srcb[i];
    }
}


/* BLAKE2s */

#define BLAKE2S_BLOCK_SIZE    64U
#define BLAKE2S_OUT_SIZE      32U
#define BLAKE2S_KEY_SIZE      32U

/* Parameter block of 32 bytes */
typedef struct blake2s_param_t {
    uchar digest_length;
    uchar key_length;
    uchar fanout;
    uchar depth;
    uint  leaf_length;
    uchar node_offset[6];
    uchar node_depth;
    uchar inner_length;
    uchar salt[8];
    uchar personal[8];
} blake2s_param;

/* State block of 180 bytes */
typedef struct blake2s_state_t {
    uint  h[8];
    uint  t[2];
    uint  f[2];
    uchar buf[2 * BLAKE2S_BLOCK_SIZE];
    uint  buflen;
} blake2s_state;

static const uint blake2s_IV[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

static const uint8_t blake2s_sigma[10][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 } ,
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 } ,
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 } ,
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 } ,
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 } ,
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 } ,
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 } ,
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 } ,
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 } ,
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 } ,
};

static void blake2s_compress(blake2s_state *S, const uint *buf) {
    uint i;
    uint m[16];
    uint v[16];

    neoscrypt_copy(m, buf, 64);
    neoscrypt_copy(v, S, 32);

    v[ 8] = blake2s_IV[0];
    v[ 9] = blake2s_IV[1];
    v[10] = blake2s_IV[2];
    v[11] = blake2s_IV[3];
    v[12] = S->t[0] ^ blake2s_IV[4];
    v[13] = S->t[1] ^ blake2s_IV[5];
    v[14] = S->f[0] ^ blake2s_IV[6];
    v[15] = S->f[1] ^ blake2s_IV[7];
#define G(r,i,a,b,c,d) \
  do { \
    a = a + b + m[blake2s_sigma[r][2*i+0]]; \
    d = ROTR32(d ^ a, 16); \
    c = c + d; \
    b = ROTR32(b ^ c, 12); \
    a = a + b + m[blake2s_sigma[r][2*i+1]]; \
    d = ROTR32(d ^ a, 8); \
    c = c + d; \
    b = ROTR32(b ^ c, 7); \
  } while(0)
#define ROUND(r) \
  do { \
    G(r, 0, v[ 0], v[ 4], v[ 8], v[12]); \
    G(r, 1, v[ 1], v[ 5], v[ 9], v[13]); \
    G(r, 2, v[ 2], v[ 6], v[10], v[14]); \
    G(r, 3, v[ 3], v[ 7], v[11], v[15]); \
    G(r, 4, v[ 0], v[ 5], v[10], v[15]); \
    G(r, 5, v[ 1], v[ 6], v[11], v[12]); \
    G(r, 6, v[ 2], v[ 7], v[ 8], v[13]); \
    G(r, 7, v[ 3], v[ 4], v[ 9], v[14]); \
  } while(0)
    ROUND(0);
    ROUND(1);
    ROUND(2);
    ROUND(3);
    ROUND(4);
    ROUND(5);
    ROUND(6);
    ROUND(7);
    ROUND(8);
    ROUND(9);

  for(i = 0; i < 8; i++)
    S->h[i] = S->h[i] ^ v[i] ^ v[i + 8];

#undef G
#undef ROUND
}

static void blake2s_update(blake2s_state *S, const uchar *input, uint input_size) {
    uint left, fill;

    while(input_size > 0) {
        left = S->buflen;
        fill = 2 * BLAKE2S_BLOCK_SIZE - left;
        if(input_size > fill) {
            /* Buffer fill */
            neoscrypt_copy(S->buf + left, input, fill);
            S->buflen += fill;
            /* Counter increment */
            S->t[0] += BLAKE2S_BLOCK_SIZE;
            /* Compress */
            blake2s_compress(S, (uint *) S->buf);
            /* Shift buffer left */
            neoscrypt_copy(S->buf, S->buf + BLAKE2S_BLOCK_SIZE, BLAKE2S_BLOCK_SIZE);
            S->buflen -= BLAKE2S_BLOCK_SIZE;
            input += fill;
            input_size -= fill;
        } else {
            neoscrypt_copy(S->buf + left, input, input_size);
            S->buflen += input_size;
            /* Do not compress */
            input += input_size;
            input_size = 0;
        }
    }
}

static void neoscrypt_blake2s(const void *input, const uint input_size, const void *key, const uchar key_size,
  void *output, const uchar output_size) {
    uchar block[BLAKE2S_BLOCK_SIZE];
    blake2s_param P[1];
    blake2s_state S[1];

    /* Initialise */
    neoscrypt_erase(P, 32);
    P->digest_length = output_size;
    P->key_length    = key_size;
    P->fanout        = 1;
    P->depth         = 1;

    neoscrypt_erase(S, 180);
    neoscrypt_copy(S, blake2s_IV, 32);
    neoscrypt_xor(S, P, 32);

    neoscrypt_erase(block, BLAKE2S_BLOCK_SIZE);
    neoscrypt_copy(block, key, key_size);
    blake2s_update(S, (uchar *) block, BLAKE2S_BLOCK_SIZE);

    /* Update */
    blake2s_update(S, (uchar *) input, input_size);

    /* Finish */
    if(S->buflen > BLAKE2S_BLOCK_SIZE) {
        S->t[0] += BLAKE2S_BLOCK_SIZE;
        blake2s_compress(S, (uint *) S->buf);
        S->buflen -= BLAKE2S_BLOCK_SIZE;
        neoscrypt_copy(S->buf, S->buf + BLAKE2S_BLOCK_SIZE, S->buflen);
    }
    S->t[0] += S->buflen;
    S->f[0] = ~0U;
    neoscrypt_erase(S->buf + S->buflen, 2 * BLAKE2S_BLOCK_SIZE - S->buflen);
    blake2s_compress(S, (uint *) S->buf);
    /* Write back */
    neoscrypt_copy(output, S, output_size);
}


#define FASTKDF_BUFFER_SIZE 256U

/* FastKDF, a fast buffered key derivation function:
 * FASTKDF_BUFFER_SIZE must be a power of 2;
 * password_len, salt_len and output_len should not exceed FASTKDF_BUFFER_SIZE;
 * prf_output_size must be <= prf_key_size; */
static void neoscrypt_fastkdf(const uchar *password, uint password_len,
							  const uchar *salt, uint salt_len,
							  uint N, uchar *output, uint output_len) {
    const uint stack_align = 0x40, kdf_buf_size = FASTKDF_BUFFER_SIZE,
			prf_input_size = BLAKE2S_BLOCK_SIZE, prf_key_size = BLAKE2S_KEY_SIZE,
			prf_output_size = BLAKE2S_OUT_SIZE;
    uint bufptr, a, b, i, j;
    uchar *A, *B, *prf_input, *prf_key, *prf_output;

    /* Align and set up the buffers in stack */
    uchar stack[2 * kdf_buf_size + prf_input_size + prf_key_size + prf_output_size + stack_align];
    A          = &stack[stack_align & ~(stack_align - 1)];
    B          = &A[kdf_buf_size + prf_input_size];
    prf_output = &A[2 * kdf_buf_size + prf_input_size + prf_key_size];

    /* Initialise the password buffer */
    if(password_len > kdf_buf_size)
       password_len = kdf_buf_size;

    a = kdf_buf_size / password_len;
    for(i = 0; i < a; i++)
      neoscrypt_copy(&A[i * password_len], &password[0], password_len);
    b = kdf_buf_size - a * password_len;
    if(b)
      neoscrypt_copy(&A[a * password_len], &password[0], b);
    neoscrypt_copy(&A[kdf_buf_size], &password[0], prf_input_size);

    /* Initialise the salt buffer */
    if(salt_len > kdf_buf_size)
       salt_len = kdf_buf_size;

    a = kdf_buf_size / salt_len;
    for(i = 0; i < a; i++)
      neoscrypt_copy(&B[i * salt_len], &salt[0], salt_len);
    b = kdf_buf_size - a * salt_len;
    if(b)
      neoscrypt_copy(&B[a * salt_len], &salt[0], b);
    neoscrypt_copy(&B[kdf_buf_size], &salt[0], prf_key_size);

    /* The primary iteration */
    for(i = 0, bufptr = 0; i < N; i++) {

        /* Map the PRF input buffer */
        prf_input = &A[bufptr];

        /* Map the PRF key buffer */
        prf_key = &B[bufptr];

        /* PRF */
        neoscrypt_blake2s(prf_input, prf_input_size, prf_key, prf_key_size, prf_output, prf_output_size);

        /* Calculate the next buffer pointer */
        for(j = 0, bufptr = 0; j < prf_output_size; j++)
          bufptr += prf_output[j];
        bufptr &= (kdf_buf_size - 1);
        /* Modify the salt buffer */
        neoscrypt_xor(&B[bufptr], &prf_output[0], prf_output_size);

        /* Head modified, tail updated */
        if(bufptr < prf_key_size)
          neoscrypt_copy(&B[kdf_buf_size + bufptr], &B[bufptr], MIN(prf_output_size, prf_key_size - bufptr));

        /* Tail modified, head updated */
        if((kdf_buf_size - bufptr) < prf_output_size)
          neoscrypt_copy(&B[0], &B[kdf_buf_size], prf_output_size - (kdf_buf_size - bufptr));
    }

    /* Modify and copy into the output buffer */
    if(output_len > kdf_buf_size)
       output_len = kdf_buf_size;

    a = kdf_buf_size - bufptr;
    if(a >= output_len) {
        neoscrypt_xor(&B[bufptr], &A[0], output_len);
        neoscrypt_copy(&output[0], &B[bufptr], output_len);
    } else {
        neoscrypt_xor(&B[bufptr], &A[0], a);
        neoscrypt_xor(&B[0], &A[a], output_len - a);
        neoscrypt_copy(&output[0], &B[bufptr], a);
        neoscrypt_copy(&output[a], &B[0], output_len - a);
    }

}


/* Configurable optimised block mixer */
static void neoscrypt_blkmix(uint *X, uint *Y, uint r, uint mixmode) {
    uint i, mixer, rounds;

    mixer  = mixmode >> 8;
    rounds = mixmode & 0xFF;

    /* NeoScrypt flow:                   Scrypt flow:
         Xa ^= Xd;  M(Xa'); Ya = Xa";      Xa ^= Xb;  M(Xa'); Ya = Xa";
         Xb ^= Xa"; M(Xb'); Yb = Xb";      Xb ^= Xa"; M(Xb'); Yb = Xb";
         Xc ^= Xb"; M(Xc'); Yc = Xc";      Xa" = Ya;
         Xd ^= Xc"; M(Xd'); Yd = Xd";      Xb" = Yb;
         Xa" = Ya; Xb" = Yc;
         Xc" = Yb; Xd" = Yd; */

    if(r == 1) {
        neoscrypt_blkxor(&X[0], &X[16], SCRYPT_BLOCK_SIZE);
        if(mixer)
          neoscrypt_chacha(&X[0], rounds);
        else
          neoscrypt_salsa(&X[0], rounds);
        neoscrypt_blkxor(&X[16], &X[0], SCRYPT_BLOCK_SIZE);
        if(mixer)
          neoscrypt_chacha(&X[16], rounds);
        else
          neoscrypt_salsa(&X[16], rounds);
        return;
    }

    if(r == 2) {
        neoscrypt_blkxor(&X[0], &X[48], SCRYPT_BLOCK_SIZE);
        if(mixer)
          neoscrypt_chacha(&X[0], rounds);
        else
          neoscrypt_salsa(&X[0], rounds);
        neoscrypt_blkxor(&X[16], &X[0], SCRYPT_BLOCK_SIZE);
        if(mixer)
          neoscrypt_chacha(&X[16], rounds);
        else
          neoscrypt_salsa(&X[16], rounds);
        neoscrypt_blkxor(&X[32], &X[16], SCRYPT_BLOCK_SIZE);
        if(mixer)
          neoscrypt_chacha(&X[32], rounds);
        else
          neoscrypt_salsa(&X[32], rounds);
        neoscrypt_blkxor(&X[48], &X[32], SCRYPT_BLOCK_SIZE);
        if(mixer)
          neoscrypt_chacha(&X[48], rounds);
        else
          neoscrypt_salsa(&X[48], rounds);
        neoscrypt_blkswp(&X[16], &X[32], SCRYPT_BLOCK_SIZE);
        return;
    }

    /* Reference code for any reasonable r */
    for(i = 0; i < 2 * r; i++) {
        if(i) neoscrypt_blkxor(&X[16 * i], &X[16 * (i - 1)], SCRYPT_BLOCK_SIZE);
        else  neoscrypt_blkxor(&X[0], &X[16 * (2 * r - 1)], SCRYPT_BLOCK_SIZE);
        if(mixer)
          neoscrypt_chacha(&X[16 * i], rounds);
        else
          neoscrypt_salsa(&X[16 * i], rounds);
        neoscrypt_blkcpy(&Y[16 * i], &X[16 * i], SCRYPT_BLOCK_SIZE);
    }
    for(i = 0; i < r; i++)
      neoscrypt_blkcpy(&X[16 * i], &Y[16 * 2 * i], SCRYPT_BLOCK_SIZE);
    for(i = 0; i < r; i++)
      neoscrypt_blkcpy(&X[16 * (i + r)], &Y[16 * (2 * i + 1)], SCRYPT_BLOCK_SIZE);
}

/* NeoScrypt core engine:
 * p = 1, salt = password;
 * Basic customisation (required):
 *   profile bit 0:
 *     0 = NeoScrypt(128, 2, 1) with Salsa20/20 and ChaCha20/20;
 *     1 = Scrypt(1024, 1, 1) with Salsa20/8;
 *   profile bits 4 to 1:
 *     0000 = FastKDF-BLAKE2s;
 *     0001 = PBKDF2-HMAC-SHA256;
 *     0010 = PBKDF2-HMAC-BLAKE256;
 * Extended customisation (optional):
 *   profile bit 31:
 *     0 = extended customisation absent;
 *     1 = extended customisation present;
 *   profile bits 7 to 5 (rfactor):
 *     000 = r of 1;
 *     001 = r of 2;
 *     010 = r of 4;
 *     ...
 *     111 = r of 128;
 *   profile bits 12 to 8 (Nfactor):
 *     00000 = N of 2;
 *     00001 = N of 4;
 *     00010 = N of 8;
 *     .....
 *     00110 = N of 128;
 *     .....
 *     01001 = N of 1024;
 *     .....
 *     11110 = N of 2147483648;
 *   profile bits 30 to 13 are reserved */
void neoscrypt(const uchar *password, uchar *output, uint profile) {
    uint N = 128, r = 2, dblmix = 1, mixmode = 0x14, stack_align = 0x40;
    uint kdf, i, j;
    uint *X, *Y, *Z, *V;

    if(profile & 0x1) {
        N = 1024;        /* N = (1 << (Nfactor + 1)); */
        r = 1;           /* r = (1 << rfactor); */
        dblmix = 0;      /* Salsa only */
        mixmode = 0x08;  /* 8 rounds */
    }

    if(profile >> 31) {
        N = (1 << (((profile >> 8) & 0x1F) + 1));
        r = (1 << ((profile >> 5) & 0x7));
    }

    uchar *stack;
    stack = (uchar *)malloc((N + 3) * r * 2 * SCRYPT_BLOCK_SIZE + stack_align);
    /* X = r * 2 * SCRYPT_BLOCK_SIZE */
    X = (uint *) &stack[stack_align & ~(stack_align - 1)];
    /* Z is a copy of X for ChaCha */
    Z = &X[32 * r];
    /* Y is an X sized temporal space */
    Y = &X[64 * r];
    /* V = N * r * 2 * SCRYPT_BLOCK_SIZE */
    V = &X[96 * r];

    /* X = KDF(password, salt) */
    kdf = (profile >> 1) & 0xF;

    switch(kdf) {

        default:
        case(0x0):
            neoscrypt_fastkdf(password, 80, password, 80, 32, (uchar *) X, r * 2 * SCRYPT_BLOCK_SIZE);
            break;

#if (SHA256)
        case(0x1):
            neoscrypt_pbkdf2_sha256(password, 80, password, 80, 1, (uchar *) X, r * 2 * SCRYPT_BLOCK_SIZE);
            break;
#endif

#if (BLAKE256)
        case(0x2):
            neoscrypt_pbkdf2_blake256(password, 80, password, 80, 1, (uchar *) X, r * 2 * SCRYPT_BLOCK_SIZE);
            break;
#endif

    }

    /* Process ChaCha 1st, Salsa 2nd and XOR them into PBKDF2; otherwise Salsa only */

    if(dblmix) {
        /* blkcpy(Z, X) */
        neoscrypt_blkcpy(&Z[0], &X[0], r * 2 * SCRYPT_BLOCK_SIZE);

        /* Z = SMix(Z) */
        for(i = 0; i < N; i++) {
            /* blkcpy(V, Z) */
            neoscrypt_blkcpy(&V[i * (32 * r)], &Z[0], r * 2 * SCRYPT_BLOCK_SIZE);
            /* blkmix(Z, Y) */
            neoscrypt_blkmix(&Z[0], &Y[0], r, (mixmode | 0x0100));
        }
        for(i = 0; i < N; i++) {
            /* integerify(Z) mod N */
            j = (32 * r) * (Z[16 * (2 * r - 1)] & (N - 1));
            /* blkxor(Z, V) */
            neoscrypt_blkxor(&Z[0], &V[j], r * 2 * SCRYPT_BLOCK_SIZE);
            /* blkmix(Z, Y) */
            neoscrypt_blkmix(&Z[0], &Y[0], r, (mixmode | 0x0100));
        }
    }


    /* X = SMix(X) */
    for(i = 0; i < N; i++) {
        /* blkcpy(V, X) */
        neoscrypt_blkcpy(&V[i * (32 * r)], &X[0], r * 2 * SCRYPT_BLOCK_SIZE);
        /* blkmix(X, Y) */
        neoscrypt_blkmix(&X[0], &Y[0], r, mixmode);
    }
    for(i = 0; i < N; i++) {
        /* integerify(X) mod N */
        j = (32 * r) * (X[16 * (2 * r - 1)] & (N - 1));
        /* blkxor(X, V) */
        neoscrypt_blkxor(&X[0], &V[j], r * 2 * SCRYPT_BLOCK_SIZE);
        /* blkmix(X, Y) */
        neoscrypt_blkmix(&X[0], &Y[0], r, mixmode);
    }


    if(dblmix)
      /* blkxor(X, Z) */
      neoscrypt_blkxor(&X[0], &Z[0], r * 2 * SCRYPT_BLOCK_SIZE);

    /* output = KDF(password, X) */
    switch(kdf) {

        default:
        case(0x0):
            neoscrypt_fastkdf(password, 80, (uchar *) X, r * 2 * SCRYPT_BLOCK_SIZE, 32, output, 32);
            break;

#if (SHA256)
        case(0x1):
            neoscrypt_pbkdf2_sha256(password, 80, (uchar *) X, r * 2 * SCRYPT_BLOCK_SIZE, 1, output, 32);
            break;
#endif

#if (BLAKE256)
        case(0x2):
            neoscrypt_pbkdf2_blake256(password, 80, (uchar *) X, r * 2 * SCRYPT_BLOCK_SIZE, 1, output, 32);
            break;
#endif

    }
    free(stack);
}

void neoscrypt_regenhash(struct work *work)
{
	neoscrypt(work->data, work->hash, 0x80000620);
}

#if (NEOSCRYPT_TEST)

#include <stdio.h>

int main() {
    uint prf_input_len = 64, prf_key_len = 32, prf_output_len = 32;
    uint kdf_input_len = 80, kdf_output_len = 256, N = 32;
    uint neoscrypt_output_len = 32;
    uchar input[kdf_input_len], output[kdf_output_len];
    uint i;
    ubool fail;

    for(i = 0; i < kdf_input_len; i++) {
        input[i] = i;
    }

    neoscrypt_blake2s(input, prf_input_len, input, prf_key_len, output, prf_output_len);

    uchar blake2s_ref[32] = {
        0x89, 0x75, 0xB0, 0x57, 0x7F, 0xD3, 0x55, 0x66,
        0xD7, 0x50, 0xB3, 0x62, 0xB0, 0x89, 0x7A, 0x26,
        0xC3, 0x99, 0x13, 0x6D, 0xF0, 0x7B, 0xAB, 0xAB,
        0xBD, 0xE6, 0x20, 0x3F, 0xF2, 0x95, 0x4E, 0xD4 };

    for(i = 0, fail = 0; i < prf_output_len; i++) {
        if(output[i] != blake2s_ref[i]) {
            fail = 1;
            break;
        }
    }

    if(fail) {
        printf("BLAKE2s integrity test failed!\n");
        return(1);
    } else {
        printf("BLAKE2s integrity test passed.\n");
    }

    neoscrypt_fastkdf(input, kdf_input_len, input, kdf_input_len, N, output, kdf_output_len);

    uchar fastkdf_ref[256] = {
        0xCC, 0xBC, 0x19, 0x71, 0xEC, 0x44, 0xE3, 0x17,
        0xB3, 0xC9, 0xDE, 0x16, 0x76, 0x02, 0x60, 0xB8,
        0xE2, 0xD4, 0x79, 0xB6, 0x88, 0xCA, 0xB5, 0x4A,
        0xCF, 0x6E, 0x0E, 0x9A, 0xAE, 0x48, 0x78, 0x12,
        0xA1, 0x95, 0x1E, 0xE1, 0xD1, 0x0A, 0xC2, 0x94,
        0x1F, 0x0A, 0x39, 0x73, 0xFE, 0xA4, 0xCD, 0x87,
        0x4B, 0x38, 0x54, 0x72, 0xB5, 0x53, 0xC3, 0xEA,
        0xC1, 0x26, 0x8D, 0xA7, 0xFF, 0x3F, 0xC1, 0x79,
        0xA6, 0xFF, 0x96, 0x54, 0x29, 0x05, 0xC0, 0x22,
        0x90, 0xDB, 0x53, 0x87, 0x2D, 0x29, 0x00, 0xA6,
        0x14, 0x16, 0x38, 0x63, 0xDA, 0xBC, 0x0E, 0x99,
        0x68, 0xB3, 0x98, 0x92, 0x42, 0xE3, 0xF6, 0xB4,
        0x19, 0xE3, 0xE3, 0xF6, 0x8E, 0x67, 0x47, 0x7B,
        0xB6, 0xFB, 0xEA, 0xCE, 0x6D, 0x0F, 0xAF, 0xF6,
        0x19, 0x43, 0x8D, 0xF7, 0x3E, 0xB5, 0xFB, 0xA3,
        0x64, 0x5E, 0xD2, 0x72, 0x80, 0x6B, 0x39, 0x93,
        0xB7, 0x80, 0x04, 0xCB, 0xF5, 0xC2, 0x61, 0xB1,
        0x90, 0x4E, 0x2B, 0x02, 0x57, 0x53, 0x77, 0x16,
        0x6A, 0x52, 0xBD, 0xD1, 0x62, 0xEC, 0xA1, 0xCB,
        0x89, 0x03, 0x29, 0xA2, 0x02, 0x5C, 0x9A, 0x62,
        0x99, 0x44, 0x54, 0xEA, 0x44, 0x91, 0x27, 0x3A,
        0x50, 0x82, 0x62, 0x03, 0x99, 0xB3, 0xFA, 0xF7,
        0xD4, 0x13, 0x47, 0x61, 0xFB, 0x0A, 0xE7, 0x81,
        0x61, 0x57, 0x58, 0x4C, 0x69, 0x4E, 0x67, 0x0A,
        0xC1, 0x21, 0xA7, 0xD2, 0xF6, 0x6D, 0x2F, 0x10,
        0x01, 0xFB, 0xA5, 0x47, 0x2C, 0xE5, 0x15, 0xD7,
        0x6A, 0xEF, 0xC9, 0xE2, 0xC2, 0x88, 0xA2, 0x3B,
        0x6C, 0x8D, 0xBB, 0x26, 0xE7, 0xC4, 0x15, 0xEC,
        0x5E, 0x5D, 0x74, 0x79, 0xBD, 0x81, 0x35, 0xA1,
        0x42, 0x27, 0xEB, 0x57, 0xCF, 0xF6, 0x2E, 0x51,
        0x90, 0xFD, 0xD9, 0xE4, 0x53, 0x6E, 0x12, 0xA1,
        0x99, 0x79, 0x4D, 0x29, 0x6F, 0x5B, 0x4D, 0x9A };

    for(i = 0, fail = 0; i < kdf_output_len; i++) {
        if(output[i] != fastkdf_ref[i]) {
            fail = 1;
            break;
        }
    }

    if(fail) {
        printf("FastKDF integrity test failed!\n");
        return(1);
    } else {
        printf("FastKDF integrity test passed.\n");
    }

    neoscrypt(input, output, 0x80000620);

    uchar neoscrypt_ref[32] = {
        0x72, 0x58, 0x96, 0x1A, 0xFB, 0x33, 0xFD, 0x12,
        0xD0, 0x0C, 0xAC, 0xB8, 0xD6, 0x3F, 0x4F, 0x4F,
        0x52, 0xBB, 0x69, 0x17, 0x04, 0x38, 0x65, 0xDD,
        0x24, 0xA0, 0x8F, 0x57, 0x88, 0x53, 0x12, 0x2D };

    for(i = 0, fail = 0; i < neoscrypt_output_len; i++) {
        if(output[i] != neoscrypt_ref[i]) {
            fail = 1;
            break;
        }
    }

    if(fail) {
        printf("NeoScrypt integrity test failed!\n");
        return(1);
    } else {
        printf("NeoScrypt integrity test passed.\n");
    }

    return(0);
}

#endif
