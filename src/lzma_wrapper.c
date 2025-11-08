#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Minimal types mirroring LZMA SDK prototypes
typedef unsigned char Byte;
typedef size_t SizeT;
typedef int SRes;

#ifndef SZ_OK
#define SZ_OK 0
#endif

// LZMA decode finish mode and status (minimal)
typedef enum { LZMA_FINISH_ANY = 0, LZMA_FINISH_END = 1 } ELzmaFinishMode;
typedef int ELzmaStatus;

// LZMA allocator interface
typedef struct ISzAlloc {
    void *(*Alloc)(struct ISzAlloc *p, size_t size);
    void (*Free)(struct ISzAlloc *p, void *address);
} ISzAlloc;

static void *SzAlloc(ISzAlloc *p, size_t size) {
    (void)p;
    return malloc(size);
}

static void SzFree(ISzAlloc *p, void *address) {
    (void)p;
    free(address);
}

// Encoder properties (layout must match LZMA SDK)
typedef struct {
    int level;
    unsigned dictSize;
    int lc;
    int lp;
    int pb;
    int fb;
    int numThreads;
} CLzmaEncProps;

// LZMA SDK external functions (declared here to avoid header dependency)
extern void LzmaEncProps_Init(CLzmaEncProps *p);
extern void LzmaEncProps_Normalize(CLzmaEncProps *p);
extern SRes LzmaEncode(Byte *dest, SizeT *destLen, const Byte *src, SizeT srcLen,
                       const CLzmaEncProps *props, Byte *propsEncoded, SizeT *propsSize,
                       int writeEndMark, void *progress);

extern SRes LzmaDecode(Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen,
                       const Byte *propData, SizeT propSize, ELzmaFinishMode finishMode,
                       ELzmaStatus *status, ISzAlloc *alloc);

// Public API
size_t lzma_compress(const uint8_t* in, size_t in_len,
                     uint8_t* out, size_t out_cap, uint32_t level /* 0-9 */);

size_t lzma_decompress(const uint8_t* in, size_t in_len,
                       uint8_t* out, size_t out_cap);

#define ONE_MB (1024u * 1024u)
#define DICT_SMALL (64u * 1024u * 1024u)
#define DICT_LARGE (512u * 1024u * 1024u)

size_t lzma_compress(const uint8_t* in, size_t in_len,
                     uint8_t* out, size_t out_cap, uint32_t level /* 0-9 */) {
    (void)level; // Per requirements, use level = 9 (ultra)

    if (!in || !out) return 0;
    if (out_cap < 6) return 0; // need space for props (5 bytes) + at least 1 byte payload

    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = 9;
    props.dictSize = (in_len >= (16u * 1024u * 1024u)) ? DICT_LARGE : DICT_SMALL;
    props.lc = 3;
    props.lp = 0;
    props.pb = 2;
    // keep defaults for fb and numThreads; normalize to ensure internal consistency
    LzmaEncProps_Normalize(&props);

    Byte propsEncoded[5];
    SizeT propsSize = sizeof(propsEncoded);
    SizeT destLen = (out_cap > propsSize) ? (out_cap - propsSize) : 0;
    if (destLen == 0) return 0;

    int writeEndMark = 1; // write end marker for safe streaming decode
    void *progress = NULL;

    SRes res = LzmaEncode((Byte*)(out + propsSize), &destLen,
                          (const Byte*)in, (SizeT)in_len,
                          &props, propsEncoded, &propsSize,
                          writeEndMark, progress);

    if (res != SZ_OK) {
        return 0;
    }

    // prepend properties
    memcpy(out, propsEncoded, propsSize);
    return (size_t)(propsSize + destLen);
}

size_t lzma_decompress(const uint8_t* in, size_t in_len,
                       uint8_t* out, size_t out_cap) {
    if (!in || !out) return 0;
    if (in_len < 5) return 0; // require 5-byte LZMA properties prefix

    const Byte *props = (const Byte*)in;
    SizeT propsSize = 5;
    const Byte *src = (const Byte*)(in + propsSize);
    SizeT srcLen = (SizeT)(in_len - propsSize);
    SizeT destLen = (SizeT)out_cap;

    ISzAlloc alloc = { SzAlloc, SzFree };
    ELzmaStatus status = 0;

    SRes res = LzmaDecode((Byte*)out, &destLen,
                          src, &srcLen,
                          props, propsSize,
                          LZMA_FINISH_ANY, &status, &alloc);

    if (res != SZ_OK) {
        return 0;
    }
    return (size_t)destLen;
}