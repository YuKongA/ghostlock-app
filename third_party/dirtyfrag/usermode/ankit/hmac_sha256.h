#pragma once
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
} sha256_ctx;

static const uint32_t _sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static inline uint32_t _rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline void sha256_transform(sha256_ctx *ctx, const uint8_t data[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)data[i*4]<<24)|((uint32_t)data[i*4+1]<<16)|((uint32_t)data[i*4+2]<<8)|(uint32_t)data[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = _rotr32(w[i-15],7)^_rotr32(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1 = _rotr32(w[i-2],17)^_rotr32(w[i-2],19)^(w[i-2]>>10);
        w[i] = w[i-16]+s0+w[i-7]+s1;
    }
    uint32_t a=ctx->state[0],b=ctx->state[1],c=ctx->state[2],d=ctx->state[3];
    uint32_t e=ctx->state[4],f=ctx->state[5],g=ctx->state[6],h=ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1=_rotr32(e,6)^_rotr32(e,11)^_rotr32(e,25);
        uint32_t ch=(e&f)^(~e&g);
        uint32_t t1=h+S1+ch+_sha256_k[i]+w[i];
        uint32_t S0=_rotr32(a,2)^_rotr32(a,13)^_rotr32(a,22);
        uint32_t maj=(a&b)^(a&c)^(b&c);
        uint32_t t2=S0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
    ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}
static inline void sha256_init(sha256_ctx *ctx) {
    ctx->state[0]=0x6a09e667;ctx->state[1]=0xbb67ae85;ctx->state[2]=0x3c6ef372;ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f;ctx->state[5]=0x9b05688c;ctx->state[6]=0x1f83d9ab;ctx->state[7]=0x5be0cd19;
    ctx->count=0; memset(ctx->buf,0,64);
}
static inline void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len) {
    size_t idx=(size_t)(ctx->count&63); ctx->count+=len;
    for (size_t i=0;i<len;i++) {
        ctx->buf[idx++]=data[i];
        if (idx==64) { sha256_transform(ctx,ctx->buf); idx=0; }
    }
}
static inline void sha256_final(sha256_ctx *ctx, uint8_t out[32]) {
    uint64_t bits=ctx->count*8; size_t idx=(size_t)(ctx->count&63);
    ctx->buf[idx++]=0x80;
    if (idx>56) { memset(ctx->buf+idx,0,64-idx); sha256_transform(ctx,ctx->buf); idx=0; }
    memset(ctx->buf+idx,0,56-idx);
    for (int i=0;i<8;i++) ctx->buf[56+i]=(uint8_t)(bits>>(56-8*i));
    sha256_transform(ctx,ctx->buf);
    for (int i=0;i<8;i++) { out[i*4]=(uint8_t)(ctx->state[i]>>24); out[i*4+1]=(uint8_t)(ctx->state[i]>>16); out[i*4+2]=(uint8_t)(ctx->state[i]>>8); out[i*4+3]=(uint8_t)(ctx->state[i]); }
}
static inline void hmac_sha256(const uint8_t *key, int key_len,
                                const uint8_t *msg, int msg_len,
                                uint8_t out[32]) {
    uint8_t k_ipad[64],k_opad[64],tk[32];
    if (key_len>64) {
        sha256_ctx hctx; sha256_init(&hctx); sha256_update(&hctx,key,(size_t)key_len); sha256_final(&hctx,tk);
        key=tk; key_len=32;
    }
    memset(k_ipad,0x36,64); memset(k_opad,0x5c,64);
    for (int i=0;i<key_len;i++) { k_ipad[i]^=key[i]; k_opad[i]^=key[i]; }
    sha256_ctx ctx; uint8_t inner[32];
    sha256_init(&ctx); sha256_update(&ctx,k_ipad,64); sha256_update(&ctx,msg,(size_t)msg_len); sha256_final(&ctx,inner);
    sha256_init(&ctx); sha256_update(&ctx,k_opad,64); sha256_update(&ctx,inner,32); sha256_final(&ctx,out);
}
