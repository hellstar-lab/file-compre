#include "../include/img_preconditioner.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static uint32_t read_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int bmp_detect_24(const uint8_t* buf, size_t len, bmp_info_t* out) {
    if (!buf || len < 54 || !out) return 0;
    if (buf[0] != 'B' || buf[1] != 'M') return 0;
    uint32_t pixel_offset = read_le32(buf + 10);
    uint32_t dib_size = read_le32(buf + 14);
    if (len < pixel_offset || dib_size < 40) return 0; // require BITMAPINFOHEADER
    uint32_t width = read_le32(buf + 18);
    uint32_t height = read_le32(buf + 22);
    uint16_t planes = read_le16(buf + 26);
    uint16_t bpp = read_le16(buf + 28);
    if (planes != 1 || bpp != 24) return 0;
    // Compression field at offset 30, must be BI_RGB (0)
    uint32_t compression = read_le32(buf + 30);
    if (compression != 0) return 0;
    // Row stride padded to 4-byte boundary
    uint32_t row_stride = ((width * 3u) + 3u) & ~3u;
    out->header_size = pixel_offset;
    out->pixel_offset = pixel_offset;
    out->width = width;
    out->height = height;
    out->bpp = bpp;
    out->row_stride = row_stride;
    return 1;
}

int bmp_sub_encode(const uint8_t* src_pixels, uint8_t* dst_pixels, const bmp_info_t* info) {
    if (!src_pixels || !dst_pixels || !info) return 0;
    uint32_t w = info->width;
    uint32_t h = info->height;
    uint32_t stride = info->row_stride;
    // BMP pixel array is typically bottom-up; we preserve order and operate per row
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* srow = src_pixels + (size_t)y * stride;
        uint8_t* drow = dst_pixels + (size_t)y * stride;
        // First pixel (3 bytes) copied as-is
        if (stride >= 3) {
            drow[0] = srow[0]; drow[1] = srow[1]; drow[2] = srow[2];
            // Remaining bytes: subtract left neighbor
            for (uint32_t x = 3; x < stride; ++x) {
                drow[x] = (uint8_t)(srow[x] - srow[x - 3]);
            }
        } else {
            // Tiny stride edge case
            memcpy(drow, srow, stride);
        }
    }
    return 1;
}

int bmp_sub_decode(const uint8_t* src_pixels, uint8_t* dst_pixels, const bmp_info_t* info) {
    if (!src_pixels || !dst_pixels || !info) return 0;
    uint32_t w = info->width;
    (void)w;
    uint32_t h = info->height;
    uint32_t stride = info->row_stride;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* srow = src_pixels + (size_t)y * stride;
        uint8_t* drow = dst_pixels + (size_t)y * stride;
        if (stride >= 3) {
            drow[0] = srow[0]; drow[1] = srow[1]; drow[2] = srow[2];
            for (uint32_t x = 3; x < stride; ++x) {
                drow[x] = (uint8_t)(srow[x] + drow[x - 3]);
            }
        } else {
            memcpy(drow, srow, stride);
        }
    }
    return 1;
}