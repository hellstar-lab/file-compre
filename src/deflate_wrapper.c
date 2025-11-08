// Drop-in DEFLATE wrapper using vendored miniz
#include <stdint.h>
#include <stddef.h>
#include "miniz.h"

// Compresses `in` into `out` with level 9 by default.
// Returns number of bytes written to `out`, or 0 on error.
size_t deflate_compress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap, int level) {
    if (!in || !out || out_cap == 0) return 0;
    mz_ulong destLen = (mz_ulong)out_cap;
    int lvl = (level > 0) ? level : 9;
    int rc = mz_compress2(out, &destLen, in, (mz_ulong)in_len, lvl);
    if (rc != MZ_OK) return 0;
    return (size_t)destLen;
}

// Decompresses `in` into `out`.
// Returns number of bytes written to `out`, or 0 on error.
size_t deflate_decompress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    if (!in || !out || out_cap == 0) return 0;
    mz_ulong destLen = (mz_ulong)out_cap;
    int rc = mz_uncompress(out, &destLen, in, (mz_ulong)in_len);
    if (rc != MZ_OK) return 0;
    return (size_t)destLen;
}