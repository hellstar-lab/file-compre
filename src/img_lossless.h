// Header-only lossless image compression (BMP/PNG/TGA 24-bit) using LZMA
// Implements PNG-style per-row filters, auto-selection per row, and LZMA (level 9)
// Exposes: img_compress(), img_decompress()

#ifndef IMG_LOSSLESS_H
#define IMG_LOSSLESS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ---- LZMA SDK minimal interface (avoid wrapper to control dict size) ----
#ifdef HAVE_LZMA
typedef unsigned char Byte;
typedef size_t SizeT;
typedef int SRes;

typedef struct {
    int level;
    unsigned dictSize;
    int lc;
    int lp;
    int pb;
    int fb;
    int numThreads;
} CLzmaEncProps;

typedef struct ISzAlloc {
    void *(*Alloc)(struct ISzAlloc *p, size_t size);
    void (*Free)(struct ISzAlloc *p, void *address);
} ISzAlloc;

typedef enum {
    LZMA_FINISH_ANY = 0,
    LZMA_FINISH_END = 1
} ELzmaFinishMode;

typedef enum {
    LZMA_STATUS_NOT_SPECIFIED = 0,
    LZMA_STATUS_FINISHED_WITH_MARK,
    LZMA_STATUS_NOT_FINISHED,
    LZMA_STATUS_NEEDS_MORE_INPUT
} ELzmaStatus;

extern void LzmaEncProps_Init(CLzmaEncProps *p);
extern void LzmaEncProps_Normalize(CLzmaEncProps *p);
extern SRes LzmaEncode(Byte *dest, SizeT *destLen, const Byte *src, SizeT srcLen,
                       const CLzmaEncProps *props, Byte *propsEncoded, SizeT *propsSize,
                       int writeEndMark, void *progress);
extern SRes LzmaDecode(Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen,
                       const Byte *propData, SizeT propSize, ELzmaFinishMode finishMode,
                       ELzmaStatus *status, ISzAlloc *alloc);
#endif

// Fallback DEFLATE inflate for PNG IDAT using miniz wrapper
extern size_t deflate_decompress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap);
// LZMA wrapper (implemented in lzma_wrapper.c)
extern size_t lzma_compress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap, uint32_t level);

// ---- Utility helpers ----
static inline uint32_t rd32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline int32_t rd32le_s(const uint8_t* p) { return (int32_t)rd32le(p); }
static inline uint32_t rd32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline uint16_t rd16le(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint16_t rd16be(const uint8_t* p) { return ((uint16_t)p[0] << 8) | (uint16_t)p[1]; }

// Paeth predictor
static inline int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? (p - a) : (a - p);
    int pb = p > b ? (p - b) : (b - p);
    int pc = p > c ? (p - c) : (c - p);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// Container header for filtered rows prior to LZMA
typedef struct {
    uint8_t  magic[4];   // 'I','M','G','L'
    uint8_t  version;    // 1
    uint8_t  format;     // 1=BMP,2=PNG,3=TGA
    uint8_t  channels;   // 3
    uint8_t  reserved;   // 0
    uint32_t width;      // LE
    uint32_t height;     // LE
    uint32_t stride;     // bytes per row (width*3)
} ImgHeader;

// Write header into buf; returns bytes written (sizeof header)
static inline size_t write_img_header(uint8_t* buf, uint32_t w, uint32_t h, uint32_t stride, uint8_t fmt) {
    ImgHeader hdr;
    hdr.magic[0] = 'I'; hdr.magic[1] = 'M'; hdr.magic[2] = 'G'; hdr.magic[3] = 'L';
    hdr.version = 1; hdr.format = fmt; hdr.channels = 3; hdr.reserved = 0;
    hdr.width = w; hdr.height = h; hdr.stride = stride;
    memcpy(buf, &hdr, sizeof(hdr));
    return sizeof(hdr);
}

static inline int read_img_header(const uint8_t* buf, size_t len, ImgHeader* out) {
    if (len < sizeof(ImgHeader)) return 0;
    memcpy(out, buf, sizeof(ImgHeader));
    if (out->magic[0] != 'I' || out->magic[1] != 'M' || out->magic[2] != 'G' || out->magic[3] != 'L') return 0;
    if (out->version != 1 || out->channels != 3) return 0;
    return 1;
}

// ---- Filter application and reversal ----
static uint8_t select_and_apply_filter(const uint8_t* row, const uint8_t* prev, uint32_t stride, uint32_t bpp, uint8_t* out_row) {
    // Compute metrics for 5 filters; store best row into out_row prefixed by filter type
    uint64_t metric[5] = {0,0,0,0,0};
    // None
    for (uint32_t i = 0; i < stride; i++) metric[0] += (uint64_t)(row[i] >= 128 ? (256 - row[i]) : row[i]);
    // Sub
    for (uint32_t i = 0; i < stride; i++) { uint8_t left = (i >= bpp) ? row[i - bpp] : 0; uint8_t v = (uint8_t)(row[i] - left); metric[1] += (uint64_t)(v >= 128 ? (256 - v) : v); }
    // Up
    for (uint32_t i = 0; i < stride; i++) { uint8_t up = prev ? prev[i] : 0; uint8_t v = (uint8_t)(row[i] - up); metric[2] += (uint64_t)(v >= 128 ? (256 - v) : v); }
    // Average
    for (uint32_t i = 0; i < stride; i++) { uint8_t left = (i >= bpp) ? row[i - bpp] : 0; uint8_t up = prev ? prev[i] : 0; uint8_t avg = (uint8_t)(((int)left + (int)up) / 2); uint8_t v = (uint8_t)(row[i] - avg); metric[3] += (uint64_t)(v >= 128 ? (256 - v) : v); }
    // Paeth
    for (uint32_t i = 0; i < stride; i++) { uint8_t a = (i >= bpp) ? row[i - bpp] : 0; uint8_t b = prev ? prev[i] : 0; uint8_t c = (prev && i >= bpp) ? prev[i - bpp] : 0; uint8_t v = (uint8_t)(row[i] - (uint8_t)paeth(a,b,c)); metric[4] += (uint64_t)(v >= 128 ? (256 - v) : v); }

    uint8_t best = 0; uint64_t bestm = metric[0];
    for (uint8_t f = 1; f < 5; f++) { if (metric[f] < bestm) { bestm = metric[f]; best = f; } }

    out_row[0] = best;
    switch (best) {
        case 0: for (uint32_t i = 0; i < stride; i++) out_row[1 + i] = row[i]; break;
        case 1: for (uint32_t i = 0; i < stride; i++) { uint8_t left = (i >= bpp) ? row[i - bpp] : 0; out_row[1 + i] = (uint8_t)(row[i] - left); } break;
        case 2: for (uint32_t i = 0; i < stride; i++) { uint8_t up = prev ? prev[i] : 0; out_row[1 + i] = (uint8_t)(row[i] - up); } break;
        case 3: for (uint32_t i = 0; i < stride; i++) { uint8_t left = (i >= bpp) ? row[i - bpp] : 0; uint8_t up = prev ? prev[i] : 0; uint8_t avg = (uint8_t)(((int)left + (int)up) / 2); out_row[1 + i] = (uint8_t)(row[i] - avg); } break;
        case 4: for (uint32_t i = 0; i < stride; i++) { uint8_t a = (i >= bpp) ? row[i - bpp] : 0; uint8_t b = prev ? prev[i] : 0; uint8_t c = (prev && i >= bpp) ? prev[i - bpp] : 0; out_row[1 + i] = (uint8_t)(row[i] - (uint8_t)paeth(a,b,c)); } break;
    }
    return best;
}

static void reverse_filter_row(uint8_t filter, const uint8_t* in, const uint8_t* prev, uint32_t stride, uint32_t bpp, uint8_t* out) {
    switch (filter) {
        case 0: for (uint32_t i = 0; i < stride; i++) out[i] = in[i]; break;
        case 1: for (uint32_t i = 0; i < stride; i++) { uint8_t left = (i >= bpp) ? out[i - bpp] : 0; out[i] = (uint8_t)(in[i] + left); } break;
        case 2: for (uint32_t i = 0; i < stride; i++) { uint8_t up = prev ? prev[i] : 0; out[i] = (uint8_t)(in[i] + up); } break;
        case 3: for (uint32_t i = 0; i < stride; i++) { uint8_t left = (i >= bpp) ? out[i - bpp] : 0; uint8_t up = prev ? prev[i] : 0; uint8_t avg = (uint8_t)(((int)left + (int)up) / 2); out[i] = (uint8_t)(in[i] + avg); } break;
        case 4: for (uint32_t i = 0; i < stride; i++) { uint8_t a = (i >= bpp) ? out[i - bpp] : 0; uint8_t b = prev ? prev[i] : 0; uint8_t c = (prev && i >= bpp) ? prev[i - bpp] : 0; uint8_t pr = (uint8_t)paeth(a,b,c); uint8_t dec = (uint8_t)(in[i] + pr); out[i] = dec; /* debug */ if (0) fprintf(stderr, "rev: i=%u a=%u b=%u c=%u pr=%u in=%u dec=%u\n", (unsigned)i, (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)pr, (unsigned)in[i], (unsigned)dec); } break;
        default: for (uint32_t i = 0; i < stride; i++) out[i] = in[i]; break;
    }
}

// ---- Format parsing ----
static int parse_bmp_24(const uint8_t* in, size_t in_len, uint32_t* w, uint32_t* h, uint32_t* stride, const uint8_t** pixel_data, uint32_t* data_stride, int* top_down) {
    if (in_len < 54) return 0;
    if (!(in[0] == 'B' && in[1] == 'M')) return 0;
    if (rd32le(in + 14) != 40) return 0; // BITMAPINFOHEADER size
    uint32_t bpp = (uint32_t)rd16le(in + 28);
    if (bpp != 24u) return 0;
    uint32_t compression = rd32le(in + 30);
    if (compression != 0u) return 0; // BI_RGB only
    int32_t ih = rd32le_s(in + 22);
    *top_down = (ih < 0);
    *h = (uint32_t)((ih < 0) ? -ih : ih);
    *w = rd32le(in + 18);
    uint32_t offset = rd32le(in + 10);
    if (offset >= in_len) return 0;
    *pixel_data = in + offset;
    *data_stride = (((*w) * 3u) + 3u) & ~3u; // with padding in file
    *stride = (*w) * 3u; // logical stride without padding
    // sanity
    if (offset + (*data_stride) * (*h) > in_len) return 0;
    return 1;
}

static int parse_tga_24(const uint8_t* in, size_t in_len, uint32_t* w, uint32_t* h, uint32_t* stride, const uint8_t** pixel_data, int* top_down) {
    if (in_len < 18) return 0;
    uint8_t id_len = in[0];
    uint8_t cmap_type = in[1];
    uint8_t img_type = in[2];
    if (img_type != 2) return 0; // type 2 = uncompressed truecolor
    if (cmap_type != 0) return 0;
    *w = rd16le(in + 12);
    *h = rd16le(in + 14);
    uint8_t bpp = in[16];
    if (bpp != 24) return 0;
    uint8_t desc = in[17];
    *top_down = ((desc & 0x20) != 0); // bit 5: origin
    size_t off = 18 + id_len;
    if (off >= in_len) return 0;
    *pixel_data = in + off;
    *stride = (*w) * 3u;
    size_t needed = (*stride) * (*h);
    if (off + needed > in_len) return 0;
    return 1;
}

static int parse_png_rgb24(const uint8_t* in, size_t in_len, uint32_t* w, uint32_t* h, uint32_t* stride, const uint8_t** idat_data, size_t* idat_len) {
    if (in_len < 8) return 0;
    static const uint8_t sig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    if (memcmp(in, sig, 8) != 0) return 0;
    size_t p = 8;
    int gotIHDR = 0;
    size_t idat_total = 0;
    // First pass to get IHDR and total IDAT length
    while (p + 12 <= in_len) {
        uint32_t len = rd32be(in + p);
        const uint8_t* type = in + p + 4;
        const uint8_t* data = in + p + 8;
        if (p + 12u + len > in_len) return 0;
        if (memcmp(type, "IHDR", 4) == 0 && len == 13) {
            *w = rd32be(data);
            *h = rd32be(data + 4);
            uint8_t bit_depth = data[8];
            uint8_t color_type = data[9];
            if (!(bit_depth == 8 && color_type == 2)) return 0; // RGB 8-bit only
            *stride = (*w) * 3u;
            gotIHDR = 1;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            idat_total += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        p += 12u + len;
    }
    if (!gotIHDR || idat_total == 0) return 0;
    // Second pass: gather IDAT payload
    uint8_t* cat = (uint8_t*)malloc(idat_total);
    if (!cat) return 0;
    size_t written = 0; p = 8;
    while (p + 12 <= in_len) {
        uint32_t len = rd32be(in + p);
        const uint8_t* type = in + p + 4;
        const uint8_t* data = in + p + 8;
        if (memcmp(type, "IDAT", 4) == 0) {
            memcpy(cat + written, data, len);
            written += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        p += 12u + len;
    }
    *idat_data = cat; *idat_len = written;
    return 1;
}

// Strip non-essential PNG chunks, preserving only IHDR, PLTE, IDAT, IEND
static int strip_png_chunks(const uint8_t* in, size_t in_len, uint8_t** out_png, size_t* out_len) {
    if (!in || in_len < 8 || !out_png || !out_len) return 0;
    static const uint8_t sig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    if (memcmp(in, sig, 8) != 0) return 0;
    // First pass: measure size of essential chunks
    size_t p = 8; size_t essential_total = 8; // include signature
    while (p + 12 <= in_len) {
        uint32_t len = rd32be(in + p);
        const uint8_t* type = in + p + 4;
        if (p + 12u + len > in_len) return 0;
        if (memcmp(type, "IHDR", 4) == 0 || memcmp(type, "PLTE", 4) == 0 || memcmp(type, "IDAT", 4) == 0 || memcmp(type, "IEND", 4) == 0) {
            essential_total += 12u + len; // length + type + data + CRC
        }
        if (memcmp(type, "IEND", 4) == 0) break;
        p += 12u + len;
    }
    // Allocate and copy
    uint8_t* buf = (uint8_t*)malloc(essential_total);
    if (!buf) return 0;
    memcpy(buf, sig, 8);
    size_t w = 8; p = 8;
    while (p + 12 <= in_len) {
        uint32_t len = rd32be(in + p);
        const uint8_t* type = in + p + 4;
        const uint8_t* data = in + p + 8;
        const uint8_t* crc = in + p + 8 + len;
        if (p + 12u + len > in_len) { free(buf); return 0; }
        if (memcmp(type, "IHDR", 4) == 0 || memcmp(type, "PLTE", 4) == 0 || memcmp(type, "IDAT", 4) == 0 || memcmp(type, "IEND", 4) == 0) {
            // write length (BE), type, data, crc
            buf[w + 0] = (uint8_t)((len >> 24) & 0xFF);
            buf[w + 1] = (uint8_t)((len >> 16) & 0xFF);
            buf[w + 2] = (uint8_t)((len >> 8) & 0xFF);
            buf[w + 3] = (uint8_t)(len & 0xFF);
            memcpy(buf + w + 4, type, 4);
            memcpy(buf + w + 8, data, len);
            memcpy(buf + w + 8 + len, crc, 4);
            w += 12u + len;
        }
        if (memcmp(type, "IEND", 4) == 0) break;
        p += 12u + len;
    }
    *out_png = buf; *out_len = w; return 1;
}

// Refilter PNG scanlines to per-row best filter (favoring Paeth when beneficial)
static int png_refilter(const uint8_t* png, size_t png_len, uint8_t** out_rows, size_t* out_len) {
    if (!png || png_len < 8 || !out_rows || !out_len) return 0;
    uint32_t w=0,h=0,stride=0; size_t idat_len=0; const uint8_t* idat=NULL;
    if (!parse_png_rgb24(png, png_len, &w, &h, &stride, &idat, &idat_len)) return 0;
    size_t scanline_size = (size_t)stride + 1u;
    size_t expect_size = (size_t)h * scanline_size;
    uint8_t* scan = (uint8_t*)malloc(expect_size);
    if (!scan) { free((void*)idat); return 0; }
    size_t got = deflate_decompress(idat, idat_len, scan, expect_size);
    free((void*)idat);
    if (got != expect_size) { free(scan); return 0; }
    // Reverse existing filters to raw rows
    uint8_t* raw = (uint8_t*)malloc((size_t)stride * h);
    if (!raw) { free(scan); return 0; }
    const uint8_t* prev = NULL; uint8_t* dst = raw;
    for (uint32_t y=0; y<h; y++) {
        const uint8_t* rowp = scan + y * scanline_size + 1;
        uint8_t f = scan[y * scanline_size + 0];
        reverse_filter_row(f, rowp, prev, stride, 3, dst);
        prev = dst; dst += stride;
    }
    free(scan);
    // Apply our filter selection per row
    size_t rows_out = (size_t)h * ((size_t)stride + 1u);
    uint8_t* rows = (uint8_t*)malloc(rows_out);
    if (!rows) { free(raw); return 0; }
    uint8_t* outp = rows; const uint8_t* prev2 = NULL; uint8_t* curdec = (uint8_t*)malloc((size_t)stride);
    if (!curdec) { free(rows); free(raw); return 0; }
    for (uint32_t y=0; y<h; y++) {
        const uint8_t* r = raw + (size_t)y * stride;
        select_and_apply_filter(r, prev2, stride, 3, outp);
        reverse_filter_row(outp[0], outp + 1, prev2, stride, 3, curdec);
        prev2 = curdec; outp += 1 + stride;
    }
    free(curdec);
    free(raw);
    *out_rows = rows; *out_len = rows_out; return 1;
}

// ---- LZMA helpers ----
static void* SzAllocFn(struct ISzAlloc *p, size_t size) { (void)p; return malloc(size); }
static void SzFreeFn(struct ISzAlloc *p, void *address) { (void)p; free(address); }
// ---- Pre-LZMA transform: byte-wise delta + MTF (BCM-like) ----
#define BCM_BLOCK_BYTES (1u<<20)
static size_t delta_encode_buf(const uint8_t* in, size_t n, uint8_t* out){ uint8_t prev=0; for(size_t i=0;i<n;i++){ uint8_t d=(uint8_t)(in[i]-prev); out[i]=d; prev=in[i]; } return n; }
static size_t delta_decode_buf(const uint8_t* in, size_t n, uint8_t* out){ uint8_t prev=0; for(size_t i=0;i<n;i++){ uint8_t v=(uint8_t)(in[i]+prev); out[i]=v; prev=v; } return n; }
static size_t mtf_encode_block(const uint8_t* in, size_t n, uint8_t* out){ uint8_t list[256]; for(int i=0;i<256;i++) list[i]=(uint8_t)i; for(size_t i=0;i<n;i++){ uint8_t c=in[i]; int idx=0; while(list[idx]!=c) idx++; out[i]=(uint8_t)idx; memmove(&list[1],&list[0],idx); list[0]=c; } return n; }
static size_t mtf_decode_block(const uint8_t* in, size_t n, uint8_t* out){ uint8_t list[256]; for(int i=0;i<256;i++) list[i]=(uint8_t)i; for(size_t i=0;i<n;i++){ int idx=in[i]; uint8_t c=list[idx]; out[i]=c; memmove(&list[1],&list[0],idx); list[0]=c; } return n; }
static int bcm_encode(const uint8_t* filtered, size_t len, uint8_t* out, size_t* out_len){ if(!filtered||!out||!out_len) return 0; uint8_t* tmp=(uint8_t*)malloc(len); if(!tmp) return 0; size_t pos=0; while(pos<len){ size_t blen=(len-pos>BCM_BLOCK_BYTES)?BCM_BLOCK_BYTES:(len-pos); delta_encode_buf(filtered+pos,blen,tmp+pos); mtf_encode_block(tmp+pos,blen,out+pos); pos+=blen; } free(tmp); *out_len=len; return 1; }
static int bcm_decode(const uint8_t* in, size_t len, uint8_t* out){ if(!in||!out) return 0; uint8_t* tmp=(uint8_t*)malloc(len); if(!tmp) return 0; size_t pos=0; while(pos<len){ size_t blen=(len-pos>BCM_BLOCK_BYTES)?BCM_BLOCK_BYTES:(len-pos); mtf_decode_block(in+pos,blen,tmp+pos); delta_decode_buf(tmp+pos,blen,out+pos); pos+=blen; } free(tmp); return 1; }
// forward declaration to avoid implicit declaration
static size_t lzma_compress_img(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap);
static size_t pack_and_lzma(const uint8_t* payload, size_t payload_size, uint8_t* out, size_t out_cap){ if(!payload||payload_size<sizeof(ImgHeader)) return 0; uint8_t* pre=(uint8_t*)malloc(payload_size); if(!pre) return 0; memcpy(pre,payload,sizeof(ImgHeader)); size_t bcm_len=0; if(!bcm_encode(payload+sizeof(ImgHeader), payload_size-sizeof(ImgHeader), pre+sizeof(ImgHeader), &bcm_len)){ free(pre); return 0; } size_t produced=lzma_compress_img(pre, sizeof(ImgHeader)+bcm_len, out, out_cap); free(pre); return produced; }

#ifdef HAVE_LZMA
static size_t lzma_compress_img(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    if (!in || !out || out_cap < 6) return 0;
    CLzmaEncProps props; LzmaEncProps_Init(&props);
    props.level = 9; // per requirement
    unsigned dict128 = 128u * 1024u * 1024u;
    props.dictSize = (unsigned)((in_len < dict128) ? in_len : dict128);
    props.lc = 3; props.lp = 0; props.pb = 2;
    LzmaEncProps_Normalize(&props);

    Byte propsEncoded[5]; SizeT propsSize = sizeof(propsEncoded);
    SizeT destLen = (out_cap > propsSize) ? (out_cap - propsSize) : 0;
    if (destLen == 0) return 0;
    int writeEndMark = 1; void* progress = NULL;
    SRes res = LzmaEncode((Byte*)(out + propsSize), &destLen, (const Byte*)in, (SizeT)in_len,
                          &props, propsEncoded, &propsSize, writeEndMark, progress);
    if (res != 0) return 0;
    memcpy(out, propsEncoded, propsSize);
    return (size_t)(propsSize + destLen);
}
#else
static size_t lzma_compress_img(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    (void)in; (void)in_len; (void)out; (void)out_cap;
    return 0; // LZMA disabled
}
#endif

#ifdef HAVE_LZMA
static size_t lzma_decompress_img(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    if (!in || !out || in_len < 5) return 0;
    const Byte* props = (const Byte*)in; SizeT propsSize = 5;
    const Byte* src = (const Byte*)(in + propsSize); SizeT srcLen = (SizeT)(in_len - propsSize);
    SizeT destLen = (SizeT)out_cap; ISzAlloc alloc = { SzAllocFn, SzFreeFn }; ELzmaStatus st = LZMA_STATUS_NOT_SPECIFIED;
    SRes res = LzmaDecode((Byte*)out, &destLen, src, &srcLen, props, propsSize, LZMA_FINISH_ANY, &st, &alloc);
    if (res != 0) return 0;
    return (size_t)destLen;
}
#else
static size_t lzma_decompress_img(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    (void)in; (void)in_len; (void)out; (void)out_cap;
    return 0; // LZMA disabled
}
#endif

// ---- Public API ----
// Compress BMP/PNG/TGA (24-bit) file bytes into filtered-row container then LZMA
static size_t img_compress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    if (!in || !out || in_len < 32) return 0;
    uint32_t w=0,h=0,stride=0; const uint8_t* pix=NULL; uint32_t file_row_stride=0; int top_down=0; uint8_t fmt=0;

    // BMP
    if (parse_bmp_24(in, in_len, &w, &h, &stride, &pix, &file_row_stride, &top_down)) {
        fmt = 1;
        size_t rowsz = (size_t)stride;
        size_t payload_size = sizeof(ImgHeader) + h * (rowsz + 1);
        uint8_t* payload = (uint8_t*)malloc(payload_size);
        if (!payload) return 0;
        write_img_header(payload, w, h, stride, fmt);
        uint8_t* outp = payload + sizeof(ImgHeader);
        // assemble top-down rows without padding (BGR maintained)
        uint8_t* curbuf = (uint8_t*)malloc(rowsz);
        uint8_t* prev_row = (uint8_t*)malloc(rowsz);
        uint8_t* curdec = (uint8_t*)malloc(rowsz);
        if (!curbuf || !prev_row || !curdec) { free(curbuf); free(prev_row); free(curdec); free(payload); return 0; }
        for (uint32_t y=0; y<h; y++) {
            uint32_t src_y = top_down ? y : (h - 1 - y);
            const uint8_t* src_row = pix + (size_t)src_y * file_row_stride;
            // copy row without padding
            memcpy(curbuf, src_row, rowsz);
            const uint8_t* prev = (y == 0) ? NULL : prev_row;
            select_and_apply_filter(curbuf, prev, stride, 3, outp);
            // reconstruct current row into a separate buffer to avoid aliasing with prev_row
            reverse_filter_row(outp[0], outp + 1, prev, stride, 3, curdec);
            // debug self-check: ensure reconstruction equals original row
            #if 1
            for (uint32_t i = 0; i < stride; i++) {
                if (curdec[i] != curbuf[i]) {
                    if (outp[0] == 4) {
                        uint8_t a = (i >= 3) ? curdec[i - 3] : 0; // decoded left
                        uint8_t b = (y == 0) ? 0 : prev[i];
                        uint8_t c = (y == 0) ? 0 : ((i >= 3) ? prev[i - 3] : 0);
                        uint8_t inb = outp[1 + i];
                        int pr = paeth(a,b,c);
                        uint8_t dec_calc = (uint8_t)(inb + (uint8_t)pr);
                        fprintf(stderr, "img_compress BMP: y=%u i=%u PAETH a=%u b=%u c=%u pr=%d in=%u dec_calc=%u orig=%u dec=%u\n",
                                (unsigned)y, (unsigned)i, (unsigned)a, (unsigned)b, (unsigned)c, pr, (unsigned)inb, (unsigned)dec_calc,
                                (unsigned)curbuf[i], (unsigned)curdec[i]);
                    } else {
                        fprintf(stderr, "img_compress BMP: y=%u first mismatch at i=%u filt=%u orig=%u dec=%u\n",
                                (unsigned)y, (unsigned)i, (unsigned)outp[0], (unsigned)curbuf[i], (unsigned)curdec[i]);
                    }
                    break;
                }
            }
            #endif
            // move decoded current row into prev_row for next iteration
            memcpy(prev_row, curdec, rowsz);
            outp += 1 + rowsz;
        }
        free(curbuf);
        free(prev_row);
        free(curdec);
        size_t produced = pack_and_lzma(payload, payload_size, out, out_cap);
        free(payload);
        return produced;
    }

    // TGA
    if (parse_tga_24(in, in_len, &w, &h, &stride, &pix, &top_down)) {
        fmt = 3;
        size_t rowsz = (size_t)stride;
        size_t payload_size = sizeof(ImgHeader) + h * (rowsz + 1);
        uint8_t* payload = (uint8_t*)malloc(payload_size);
        if (!payload) return 0;
        write_img_header(payload, w, h, stride, fmt);
        uint8_t* outp = payload + sizeof(ImgHeader);
        uint8_t* curbuf = (uint8_t*)malloc(rowsz);
        uint8_t* prev_row = (uint8_t*)malloc(rowsz);
        uint8_t* curdec = (uint8_t*)malloc(rowsz);
        if (!curbuf || !prev_row || !curdec) { free(curbuf); free(prev_row); free(curdec); free(payload); return 0; }
        for (uint32_t y=0; y<h; y++) {
            uint32_t src_y = top_down ? y : (h - 1 - y);
            const uint8_t* src_row = pix + (size_t)src_y * stride;
            memcpy(curbuf, src_row, rowsz);
            const uint8_t* prev = (y == 0) ? NULL : prev_row;
            select_and_apply_filter(curbuf, prev, stride, 3, outp);
            reverse_filter_row(outp[0], outp + 1, prev, stride, 3, curdec);
            memcpy(prev_row, curdec, rowsz);
            outp += 1 + rowsz;
        }
        free(curbuf);
        free(prev_row);
        free(curdec);
        size_t produced = pack_and_lzma(payload, payload_size, out, out_cap);
        free(payload);
        return produced;
    }

    // PNG RGB 24-bit: strip metadata, refilter rows, then LZMA compress
    const uint8_t* idat = NULL; size_t idat_len = 0;
    if (parse_png_rgb24(in, in_len, &w, &h, &stride, &idat, &idat_len)) {
        (void)idat; (void)idat_len; fmt = 2;
        uint8_t* tmp1 = NULL; size_t tmp1_len = 0;
        uint8_t* tmp2 = NULL; size_t tmp2_len = 0;
        if (!strip_png_chunks(in, in_len, &tmp1, &tmp1_len)) {
            return 0;
        }
        if (!png_refilter(tmp1, tmp1_len, &tmp2, &tmp2_len)) {
            free(tmp1);
            return 0;
        }
        uint8_t* bcm_buf = (uint8_t*)malloc(tmp2_len);
        size_t bcm_len = 0;
        bcm_encode(tmp2, tmp2_len, bcm_buf, &bcm_len);   // 1 MiB blocks
        size_t produced = lzma_compress(bcm_buf, bcm_len, out, out_cap, 9); // 512 MiB dict via wrapper
        // Post-compression verification: if ratio > 0.40, warn and re-run strip+filter
        if (produced > 0) {
            double ratio = (double)produced / (double)in_len;
            if (ratio > 0.40) {
                // Escape literal percent to avoid -Wformat parsing issues
                fprintf(stderr, "\xE2\x9A\xA0\xEF\xB8\x8F Image > 40%% – stripping metadata & re-filtering...\n");
                free(tmp1); free(tmp2); free(bcm_buf);
                tmp1 = NULL; tmp2 = NULL; bcm_buf = NULL; tmp1_len = 0; tmp2_len = 0; bcm_len = 0;
                if (strip_png_chunks(in, in_len, &tmp1, &tmp1_len) && png_refilter(tmp1, tmp1_len, &tmp2, &tmp2_len)) {
                    bcm_buf = (uint8_t*)malloc(tmp2_len);
                    bcm_len = 0;
                    bcm_encode(tmp2, tmp2_len, bcm_buf, &bcm_len);   // 1 MiB blocks
                    produced = lzma_compress(bcm_buf, bcm_len, out, out_cap, 9); // 512 MiB dict via wrapper
                }
            }
        }
        free(tmp1);
        free(tmp2);
        free(bcm_buf);
        return produced;
    }

    return 0; // unsupported format
}

// Decompress container created by img_compress and reconstruct raw RGB/BGR rows (top-down)
// Prototype carries the attribute before declarator to satisfy GCC placement rules
static size_t __attribute__((unused)) img_decompress(const uint8_t* cmp, size_t cmp_len, uint8_t* out, size_t out_cap);
static size_t img_decompress(const uint8_t* cmp, size_t cmp_len, uint8_t* out, size_t out_cap) {
    if (!cmp || !out) return 0;
    // First, LZMA-decompress to temporary buffer (we don't know payload size; try out_cap)
    // Strategy: allocate a working buffer equal to out_cap + header + space; if insufficient, fail.
    uint8_t* payload = (uint8_t*)malloc(out_cap + sizeof(ImgHeader));
    if (!payload) return 0;
    size_t got = lzma_decompress_img(cmp, cmp_len, payload, out_cap + sizeof(ImgHeader));
    // debug: guard against small got
    // fprintf(stderr, "img_decompress: cmp_len=%zu, got=%zu, cap=%zu\n", (size_t)cmp_len, (size_t)got, (size_t)(out_cap + sizeof(ImgHeader)));
    if (got < sizeof(ImgHeader)) { free(payload); return 0; }
    ImgHeader hdr; if (!read_img_header(payload, got, &hdr)) { free(payload); return 0; }
    uint32_t w = hdr.width, h = hdr.height, stride = hdr.stride;
    size_t needed = (size_t)w * (size_t)h * 3u;
    if (needed > out_cap) { free(payload); return 0; }
    const uint8_t* p = payload + sizeof(ImgHeader);
    size_t sl = (size_t)h * ((size_t)stride + 1u);
    uint8_t* filt = (uint8_t*)malloc(sl);
    if (!filt) { free(payload); return 0; }
    if (!bcm_decode(p, sl, filt)) { free(filt); free(payload); return 0; }
    p = filt;
    uint8_t* prev = NULL; uint8_t* dst = out;
    for (uint32_t y=0; y<h; y++) {
        uint8_t f = p[0]; const uint8_t* r = p + 1;
        reverse_filter_row(f, r, prev, stride, 3, dst);
        prev = dst; dst += stride; p += 1 + stride;
    }
    free(filt);
    free(payload);
    return needed;
}

typedef enum {
    IMG_NONE = 0,
    IMG_BMP, IMG_PNG, IMG_TGA, IMG_JPEG, IMG_HEIC, IMG_WEBP
} ImgType;

ImgType img_detect(const uint8_t* hdr, size_t hdr_len) {
    if (!hdr || hdr_len == 0) return IMG_NONE;
    if (hdr_len >= 3 && hdr[0] == 0xFF && hdr[1] == 0xD8 && hdr[2] == 0xFF) return IMG_JPEG; // JPEG
    if (hdr_len >= 8 && hdr[0] == 0x89 && hdr[1] == 'P' && hdr[2] == 'N' && hdr[3] == 'G' &&
        hdr[4] == 0x0D && hdr[5] == 0x0A && hdr[6] == 0x1A && hdr[7] == 0x0A) return IMG_PNG; // PNG
    if (hdr_len >= 2 && hdr[0] == 'B' && hdr[1] == 'M') return IMG_BMP; // BMP
    if (hdr_len >= 12 && memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WEBP", 4) == 0) return IMG_WEBP; // WebP
    if (hdr_len >= 12 && memcmp(hdr + 4, "ftyp", 4) == 0 &&
        (memcmp(hdr + 8, "heic", 4) == 0 || memcmp(hdr + 8, "mif1", 4) == 0)) return IMG_HEIC; // HEIC
    if (hdr_len >= 16) {
        const char* sig = "TRUEVISION-XFILE";
        if (memcmp(hdr + hdr_len - 16, sig, 16) == 0) return IMG_TGA; // TGA (footer signature)
    }
    return IMG_NONE;
}

// (Duplicates of ImgType/img_detect removed)

// ---- Format-specific lossless transformation and re-encoding ----
extern size_t deflate_compress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap, int level);

size_t img_reencode_lossless(const uint8_t* in, size_t in_len,
                             uint8_t* out, size_t out_cap) {
    if (!in || !out || in_len == 0 || out_cap == 0) return 0;
    ImgType t = img_detect(in, in_len > 64 ? 64 : in_len);
    // JPEG: extract SOF/DHT/scan payload, re-encode payload with zlib-9 into compact container
    if (t == IMG_JPEG) {
        const uint8_t* p = in; size_t pos = 0; int sof = -1;
        (void)p; // silence unused variable warning
        size_t dht_off = 0, dht_len = 0, dqt_off = 0, dqt_len = 0;
        size_t sos_off = 0, sos_hdr_len = 0, scan_off = 0, scan_len = 0;
        if (!(in_len >= 3 && in[0] == 0xFF && in[1] == 0xD8 && in[2] == 0xFF)) return 0;
        pos = 2; // after SOI marker start
        while (pos + 4 <= in_len) {
            if (in[pos] != 0xFF) { pos++; continue; }
            uint8_t marker = in[pos + 1]; pos += 2;
            if (marker == 0xD9) break; // EOI
            if (marker == 0xDA) { // SOS
                sos_off = pos - 2; sos_hdr_len = (size_t)rd16be(in + pos);
                scan_off = pos + 2 + sos_hdr_len; // skip header
                // scan ends at next 0xFFD9 or any 0xFFxx marker start
                size_t s = scan_off;
                while (s + 1 < in_len) {
                    if (in[s] == 0xFF && in[s + 1] != 0x00) { break; }
                    s++;
                }
                scan_len = s - scan_off; pos = s; continue;
            }
            size_t seg_len = (size_t)rd16be(in + pos);
            if (marker == 0xC0 || marker == 0xC2) sof = marker; // SOF0 or SOF2
            if (marker == 0xC4) { if (!dht_off) dht_off = pos + 2; dht_len += seg_len - 2; }
            if (marker == 0xDB) { if (!dqt_off) dqt_off = pos + 2; dqt_len += seg_len - 2; }
            pos += seg_len;
        }
        if (scan_len == 0) return 0;
        (void)sos_off; (void)sos_hdr_len; // values tracked for debugging; not used in final container
        // Build compact container: ["JPGR"][sof u8][dht_len u32][dqt_len u32][scan_len u32][zcmp_len u32][dht...][dqt...][zcmp]
        size_t hdr_sz = 4 + 1 + 4 + 4 + 4 + 4;
        if (out_cap < hdr_sz) return 0;
        memcpy(out, "JPGR", 4); out[4] = (uint8_t)(sof == 0xC2 ? 2 : 0);
        *(uint32_t*)(out + 5)  = dht_len; *(uint32_t*)(out + 9)  = dqt_len;
        *(uint32_t*)(out + 13) = (uint32_t)scan_len; *(uint32_t*)(out + 17) = 0; // zcmp_len placeholder
        size_t wr = hdr_sz;
        if (dht_len && dht_off) { if (wr + dht_len > out_cap) return 0; memcpy(out + wr, in + dht_off, dht_len); wr += dht_len; }
        if (dqt_len && dqt_off) { if (wr + dqt_len > out_cap) return 0; memcpy(out + wr, in + dqt_off, dqt_len); wr += dqt_len; }
        // zlib-9 compress scan payload
        size_t zcap = out_cap - wr;
        size_t zlen = deflate_compress(in + scan_off, scan_len, out + wr, zcap, 9);
        if (zlen == 0) {
            return 0;
        }
        *(uint32_t*)(out + 17) = (uint32_t)zlen;
        return wr + zlen;
    }
    // PNG: strip non-critical chunks, reconstruct filtered rows with Paeth, recompress IDAT with zlib-9
    if (t == IMG_PNG) {
        if (in_len < 8 || memcmp(in, "\x89PNG\x0D\x0A\x1A\x0A", 8) != 0) return 0;
        uint32_t w = 0, h = 0; uint8_t bit_depth = 0, color_type = 0;
        size_t pos = 8; size_t idat_total = 0; uint8_t* idat = NULL;
        while (pos + 12 <= in_len) {
            uint32_t len = rd32be(in + pos); uint32_t type = rd32be(in + pos + 4);
            const uint8_t* data = in + pos + 8; size_t next = pos + 12 + len;
            if (type == 0x49484452 /*IHDR*/ && len >= 13) {
                w = rd32be(data); h = rd32be(data + 4); bit_depth = data[8]; color_type = data[9];
            } else if (type == 0x49444154 /*IDAT*/ && len > 0) {
                uint8_t* n = (uint8_t*)malloc(idat_total + len);
                if (!n) { free(idat); return 0; }
                if (idat) { memcpy(n, idat, idat_total); free(idat); }
                memcpy(n + idat_total, data, len); idat = n; idat_total += len;
            }
            pos = next;
            if (type == 0x49454E44 /*IEND*/) break;
        }
        if (!idat || w == 0 || h == 0) { free(idat); return 0; }
        (void)bit_depth; // currently unused in row estimation; kept for future color-type logic
        // Inflate IDAT
        size_t est = (size_t)h * ((size_t)w * ((color_type==2||color_type==6)?3:1) + 1) + 1024;
        uint8_t* rows = (uint8_t*)malloc(est);
        if (!rows) { free(idat); return 0; }
        size_t got = deflate_decompress(idat, idat_total, rows, est);
        free(idat);
        if (got == 0) { free(rows); return 0; }
        // Apply Paeth to each row
        size_t stride = (size_t)w * ((color_type==2||color_type==6)?3:1);
        uint8_t* filt = (uint8_t*)malloc((size_t)h * (stride + 1));
        if (!filt) { free(rows); return 0; }
        uint8_t* prev = NULL; uint8_t* outp = filt;
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t* row = rows + y * (stride + 1) + 1; // skip original filter byte
            select_and_apply_filter(row, prev, stride, 3, outp);
            prev = outp + 1; outp += stride + 1;
        }
        free(rows);
        // Recompress filtered rows
        size_t zlen = deflate_compress(filt, (size_t)h * (stride + 1), out, out_cap, 9);
        free(filt);
        return zlen;
    }
    // HEIC/WebP: extract tile-like blocks, reorder by residual estimate, compress with LZMA-9
    if (t == IMG_HEIC || t == IMG_WEBP) {
        // Chunk input into 4KiB blocks and compute residual (sum of absolute deltas)
        const size_t blk = 4096; size_t n = (in_len + blk - 1) / blk;
        typedef struct { size_t off; size_t len; uint64_t score; } B;
        B* arr = (B*)malloc(n * sizeof(B)); if (!arr) return 0;
        for (size_t i = 0; i < n; i++) {
            size_t off = i * blk; size_t len = (off + blk <= in_len) ? blk : (in_len - off);
            uint64_t s = 0; for (size_t j = 1; j < len; j++) s += (uint64_t)(abs((int)in[off + j] - (int)in[off + j - 1]));
            arr[i].off = off; arr[i].len = len; arr[i].score = s;
        }
        // Sort ascending by score (lower residual first)
        for (size_t i = 0; i + 1 < n; i++) {
            for (size_t j = i + 1; j < n; j++) {
                if (arr[j].score < arr[i].score) { B tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp; }
            }
        }
        // Build reordered buffer
        uint8_t* tmp = (uint8_t*)malloc(in_len + n * sizeof(uint32_t)); if (!tmp) { free(arr); return 0; }
        size_t wr = 0; for (size_t i = 0; i < n; i++) { memcpy(tmp + wr, in + arr[i].off, arr[i].len); wr += arr[i].len; }
        free(arr);
        // LZMA-9 compress
        size_t zlen = lzma_compress(tmp, wr, out, out_cap, 9);
        free(tmp);
        return zlen;
    }
    // Fallback: return 0
    return 0;
}

#endif // IMG_LOSSLESS_H