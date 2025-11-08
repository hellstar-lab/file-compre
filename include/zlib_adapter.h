#ifndef ZLIB_ADAPTER_H
#define ZLIB_ADAPTER_H

#include <stdint.h>
#include <stddef.h>

/* Compress input buffer to output buffer using zlib if available.
 * Returns 0 on success, -1 on error.
 * On success, *out_size is set to the compressed size.
 */
int za_compress_buffer(const unsigned char* in, size_t in_size,
                       unsigned char* out, size_t out_capacity, size_t* out_size);

/* Decompress input buffer to output buffer using zlib if available.
 * Returns 0 on success, -1 on error.
 * On success, *out_size is set to the decompressed size.
 */
int za_decompress_buffer(const unsigned char* in, size_t in_size,
                         unsigned char* out, size_t out_capacity, size_t* out_size);

#endif /* ZLIB_ADAPTER_H */