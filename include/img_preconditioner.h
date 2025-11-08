// Lossless BMP preconditioner: PNG Sub filter per row
// Encodes/decodes 24-bit BMP pixel data to improve DEFLATE compression.

#ifndef IMG_PRECONDITIONER_H
#define IMG_PRECONDITIONER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t header_size;   // Size of BMP header (offset to pixel data)
    uint32_t pixel_offset;  // File offset where pixel data begins
    uint32_t width;         // Pixels
    uint32_t height;        // Pixels
    uint16_t bpp;           // Bits per pixel (expect 24)
    uint32_t row_stride;    // Bytes per row including padding
} bmp_info_t;

// IMGF prelude format placed at start of compressed payload when preconditioning is used
typedef struct {
    char magic[4];      // "IMGF"
    uint8_t version;    // 1
    uint8_t transform;  // 1 = SUB filter
    uint16_t reserved16;
    uint32_t header_size;
    uint32_t width;
    uint32_t height;
    uint16_t bpp;
    uint16_t reserved;  // pad
    uint32_t row_stride;
} imgf_prelude_t;

// Detect 24-bit BMP and fill bmp_info_t; returns 1 if detected, 0 otherwise
int bmp_detect_24(const uint8_t* buf, size_t len, bmp_info_t* out);

// Encode pixel data using SUB filter per row (left predictor), in-place algorithm into out buffer
// Returns 1 on success
int bmp_sub_encode(const uint8_t* src_pixels, uint8_t* dst_pixels, const bmp_info_t* info);

// Decode SUB filter back to original pixels; returns 1 on success
int bmp_sub_decode(const uint8_t* src_pixels, uint8_t* dst_pixels, const bmp_info_t* info);

#endif // IMG_PRECONDITIONER_H