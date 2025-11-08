#include "../include/comp_container.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool write_u64_le(FILE* f, uint64_t v) {
    unsigned char b[8];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
    b[4] = (unsigned char)((v >> 32) & 0xFF);
    b[5] = (unsigned char)((v >> 40) & 0xFF);
    b[6] = (unsigned char)((v >> 48) & 0xFF);
    b[7] = (unsigned char)((v >> 56) & 0xFF);
    return fwrite(b, 1, 8, f) == 8;
}

static bool read_u64_le(FILE* f, uint64_t* out) {
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8) return false;
    *out = ((uint64_t)b[0]) |
           ((uint64_t)b[1] << 8) |
           ((uint64_t)b[2] << 16) |
           ((uint64_t)b[3] << 24) |
           ((uint64_t)b[4] << 32) |
           ((uint64_t)b[5] << 40) |
           ((uint64_t)b[6] << 48) |
           ((uint64_t)b[7] << 56);
    return true;
}

static bool write_u32_le(FILE* f, uint32_t v) {
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
    return fwrite(b, 1, 4, f) == 4;
}

static bool read_u32_le(FILE* f, uint32_t* out) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *out = ((uint32_t)b[0]) |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return true;
}

bool comp_write_header(FILE* f, const comp_header_t* hdr) {
    if (!f || !hdr) return false;
    if (fwrite(hdr->magic, 1, 4, f) != 4) return false;
    if (fputc(hdr->version, f) == EOF) return false;
    if (fputc(hdr->algorithm, f) == EOF) return false;
    if (!write_u32_le(f, hdr->reserved)) return false;
    if (!write_u64_le(f, hdr->original_size)) return false;
    if (!write_u64_le(f, hdr->compressed_size)) return false;
    if (!write_u32_le(f, hdr->crc32)) return false;
    char extbuf[16] = {0};
    if (hdr->ext[0] != '\0') {
        /* Safe copy with guaranteed NUL-termination, avoids -Wstringop-truncation */
        snprintf(extbuf, sizeof extbuf, "%s", hdr->ext);
    }
    if (fwrite(extbuf, 1, sizeof(extbuf), f) != sizeof(extbuf)) return false;
    return true;
}

bool comp_read_header(FILE* f, comp_header_t* hdr) {
    if (!f || !hdr) return false;
    if (fread(hdr->magic, 1, 4, f) != 4) return false;
    hdr->version = (uint8_t)fgetc(f);
    hdr->algorithm = (uint8_t)fgetc(f);
    if (!read_u32_le(f, (uint32_t*)&hdr->reserved)) return false;
    if (!read_u64_le(f, &hdr->original_size)) return false;
    if (!read_u64_le(f, &hdr->compressed_size)) return false;
    if (!read_u32_le(f, &hdr->crc32)) return false;
    if (fread(hdr->ext, 1, sizeof(hdr->ext), f) != sizeof(hdr->ext)) return false;
    return true;
}

bool comp_validate_header(const comp_header_t* hdr) {
    if (!hdr) return false;
    if (memcmp(hdr->magic, COMP_HDR_MAGIC, 4) != 0) return false;
    if (hdr->version != COMP_HDR_VERSION) return false;
    /* Allow ZLIB and IMAGE algorithms; raw STORE disabled */
    if (!(hdr->algorithm == COMP_ALGO_ZLIB || hdr->algorithm == COMP_ALGO_IMAGE)) return false;
    if (hdr->original_size == 0) return false;
    if (hdr->compressed_size == 0) return false;
    return true;
}

void comp_fill_header(comp_header_t* hdr, comp_algo_t algo, uint64_t orig_size,
                      uint64_t comp_size, uint32_t crc32, const char* ext) {
    memcpy(hdr->magic, COMP_HDR_MAGIC, 4);
    hdr->version = COMP_HDR_VERSION;
    hdr->algorithm = (uint8_t)algo;
    hdr->reserved = 0;
    hdr->original_size = orig_size;
    hdr->compressed_size = comp_size;
    hdr->crc32 = crc32;
    memset(hdr->ext, 0, sizeof(hdr->ext));
    if (ext && ext[0] != '\0') {
        size_t n = strlen(ext);
        if (n >= sizeof(hdr->ext)) n = sizeof(hdr->ext) - 1;
        memcpy(hdr->ext, ext, n);
        hdr->ext[n] = '\0';
    }
}