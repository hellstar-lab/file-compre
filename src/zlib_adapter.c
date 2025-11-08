#include "zlib_adapter.h"
#include <string.h>
#include <stdint.h>

#ifdef USE_ZLIB
#include <zlib.h>
#endif
#ifdef USE_MINIZ
// Route through our DEFLATE wrapper (uses miniz internally)
size_t deflate_compress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap, int level);
size_t deflate_decompress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap);
#endif

int za_compress_buffer(const unsigned char* in, size_t in_size,
                       unsigned char* out, size_t out_capacity, size_t* out_size) {
    if (!in || !out || !out_size) return -1;
#if defined(USE_ZLIB)
    uLongf destLen = (uLongf)out_capacity;
    int level = 9;   /* MAX COMPRESSION */
    int rc = compress2(out, &destLen, in, (uLong)in_size, level);
    if (rc != Z_OK) return -1;
    *out_size = (size_t)destLen;
    return 0;
#elif defined(USE_MINIZ)
    size_t produced = deflate_compress((const uint8_t*)in, (size_t)in_size,
                                       (uint8_t*)out, (size_t)out_capacity, 9);
    if (produced == 0) return -1;
    *out_size = produced;
    return 0;
#else
    /* FORCE-COMPRESS – raw storage disabled */
    return -1;
#endif
}

int za_decompress_buffer(const unsigned char* in, size_t in_size,
                         unsigned char* out, size_t out_capacity, size_t* out_size) {
    if (!in || !out || !out_size) return -1;
#if defined(USE_ZLIB)
    uLongf destLen = (uLongf)out_capacity;
    int rc = uncompress(out, &destLen, in, (uLong)in_size);
    if (rc != Z_OK) return -1;
    *out_size = (size_t)destLen;
    return 0;
#elif defined(USE_MINIZ)
    size_t produced = deflate_decompress((const uint8_t*)in, (size_t)in_size,
                                         (uint8_t*)out, (size_t)out_capacity);
    if (produced == 0) return -1;
    *out_size = produced;
    return 0;
#else
    /* FORCE-COMPRESS – raw storage disabled */
    return -1;
#endif
}