#include "../include/compressor.h"
#include "img_lossless.h"
#include "../third_party/miniz/miniz.h"

#include <string.h>
#include <stdint.h>

#ifndef SZ_OK
#define SZ_OK 0
#endif

/*
 * Advanced image compression/decompression using img_lossless container
 * for 24-bit BMP/PNG/TGA with PNG-style filters + LZMA.
 * Decompression outputs a valid PNG file encoded via miniz.
 */

int image_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size, int level) {
    (void)level; // Level currently unused in img_lossless path
    if (!input || input_size <= 0 || !output || !output_size) return -1;

    // Allocate a conservative buffer for the custom container
    size_t out_cap = (size_t)input_size + ((size_t)input_size >> 1) + 65536;
    unsigned char* out_buf = (unsigned char*)COMP_MALLOC(out_cap);
    if (!out_buf) return -1;

    size_t written = img_compress(input, (size_t)input_size, out_buf, out_cap);
    if (written == 0) {
        COMP_FREE(out_buf);
        return -1;
    }

    *output = out_buf;
    *output_size = (long)written;
    return 0;
}

int image_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    if (!input || input_size <= 0 || !output || !output_size) return -1;
#ifdef HAVE_LZMA
    // First, partially decode LZMA to read the ImgHeader from the container
    if ((size_t)input_size < 6) return -1; // must contain props + at least some data

    const unsigned char* props = input;
    const unsigned char* comp_stream = input + 5;
    size_t comp_stream_len = (size_t)input_size - 5;

    ImgHeader hdr;
    SizeT hdr_out_len = sizeof(ImgHeader);
    SizeT hdr_in_len = comp_stream_len;
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    ISzAlloc alloc = { SzAllocFn, SzFreeFn };
    SRes res = LzmaDecode((Byte*)&hdr, &hdr_out_len,
                          (const Byte*)comp_stream, &hdr_in_len,
                          (const Byte*)props, 5, LZMA_FINISH_ANY, &status, &alloc);
    if (res != SZ_OK || hdr_out_len < sizeof(ImgHeader)) {
        return -1;
    }

    // Validate header basic fields
    if (hdr.channels != 3 || hdr.width == 0 || hdr.height == 0 || hdr.stride != hdr.width * hdr.channels) {
        return -1;
    }

    // Now decode full payload: header + filtered rows
    size_t rows_payload = (size_t)hdr.height * ((size_t)hdr.stride + 1);
    size_t full_payload = sizeof(ImgHeader) + rows_payload;

    unsigned char* payload = (unsigned char*)COMP_MALLOC(full_payload);
    if (!payload) return -1;

    SizeT out_len = full_payload;
    SizeT in_len = comp_stream_len;
    status = LZMA_STATUS_NOT_SPECIFIED;
    res = LzmaDecode((Byte*)payload, &out_len,
                     (const Byte*)comp_stream, &in_len,
                     (const Byte*)props, 5, LZMA_FINISH_ANY, &status, &alloc);
    if (res != SZ_OK || out_len < full_payload) {
        COMP_FREE(payload);
        return -1;
    }

    const unsigned char* row_data = payload + sizeof(ImgHeader);
    // Reconstruct raw RGB24 pixels by reversing PNG-style filters
    unsigned char* rgb = (unsigned char*)COMP_MALLOC((size_t)hdr.height * (size_t)hdr.stride);
    if (!rgb) {
        COMP_FREE(payload);
        return -1;
    }

    for (size_t y = 0; y < (size_t)hdr.height; y++) {
        const unsigned char* row = row_data + y * ((size_t)hdr.stride + 1);
        unsigned char filter_type = row[0];
        const unsigned char* filtered = row + 1;
        unsigned char* out_row = rgb + y * (size_t)hdr.stride;
        const unsigned char* prev_row = (y > 0) ? (rgb + (y - 1) * (size_t)hdr.stride) : NULL;
        reverse_filter_row(filter_type, filtered, prev_row, (uint32_t)hdr.stride, (uint32_t)hdr.channels, out_row);
    }

    // Encode to PNG in memory using miniz
    size_t png_len = 0;
    void* png_mem = tdefl_write_image_to_png_file_in_memory_ex(
        (const void*)rgb,
        (int)hdr.width,
        (int)hdr.height,
        3 /* num channels */,
        &png_len,
        MZ_DEFAULT_LEVEL,
        MZ_FALSE /* no flip */);

    if (!png_mem || png_len == 0) {
        if (png_mem) mz_free(png_mem);
        COMP_FREE(rgb);
        COMP_FREE(payload);
        return -1;
    }

    // Return PNG bytes via COMP_MALLOC-owned buffer
    unsigned char* out_png = (unsigned char*)COMP_MALLOC(png_len);
    if (!out_png) {
        mz_free(png_mem);
        COMP_FREE(rgb);
        COMP_FREE(payload);
        return -1;
    }
    memcpy(out_png, png_mem, png_len);
    mz_free(png_mem);

    COMP_FREE(rgb);
    COMP_FREE(payload);

    *output = out_png;
    *output_size = (long)png_len;
    return 0;
#else
    // LZMA is disabled; cannot decode img_lossless container
    return -1;
#endif
}