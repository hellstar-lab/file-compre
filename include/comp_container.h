#ifndef COMP_CONTAINER_H
#define COMP_CONTAINER_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

/*
 * Simple COMP container header for universal compressor/decompressor
 * Layout (little-endian):
 *  - magic[4]    : "COMP"
 *  - version[1]  : 1
 *  - algorithm[1]: 0=STORE, 1=ZLIB
 *  - reserved[2] : padding
 *  - original_size[8]
 *  - compressed_size[8]
 *  - crc32[4]    : CRC32 of original payload
 *  - ext[16]     : original file extension (null-terminated)
 */

#define COMP_HDR_MAGIC      "COMP"
#define COMP_HDR_VERSION    1

typedef enum {
    COMP_ALGO_STORE = 0,
    COMP_ALGO_ZLIB  = 1,
    COMP_ALGO_IMAGE = 2
} comp_algo_t;

typedef struct {
    char     magic[4];
    uint8_t  version;
    uint8_t  algorithm;
    uint16_t reserved;
    uint64_t original_size;
    uint64_t compressed_size;
    uint32_t crc32;
    char     ext[16];
} comp_header_t;

/* Write header to file (returns true on success) */
bool comp_write_header(FILE* f, const comp_header_t* hdr);

/* Read header from file (returns true on success) */
bool comp_read_header(FILE* f, comp_header_t* hdr);

/* Validate header fields */
bool comp_validate_header(const comp_header_t* hdr);

/* Populate header helper */
void comp_fill_header(comp_header_t* hdr, comp_algo_t algo, uint64_t orig_size,
                      uint64_t comp_size, uint32_t crc32, const char* ext);

#endif /* COMP_CONTAINER_H */