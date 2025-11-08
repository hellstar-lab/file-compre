// Drop-in DEFLATE wrapper using vendored miniz
#include <stdint.h>
#include <stddef.h>
// Explicitly include vendored miniz headers via relative path
#include "../third_party/miniz/miniz.h"
#include "../third_party/miniz/miniz_tdef.h"
#include "../third_party/miniz/miniz_tinfl.h"

// Compresses `in` into `out` with zlib header using miniz tdefl API.
// Returns number of bytes written to `out`, or 0 on error.
size_t deflate_compress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap, int level) {
    if (!in || !out || out_cap == 0) return 0;
    int lvl = (level > 0) ? level : MZ_DEFAULT_LEVEL;
    int comp_flags = (int)tdefl_create_comp_flags_from_zip_params(lvl, 15 /* zlib */, MZ_DEFAULT_STRATEGY) | TDEFL_WRITE_ZLIB_HEADER;
    size_t written = tdefl_compress_mem_to_mem(out, out_cap, in, in_len, comp_flags);
    return written ? written : 0;
}

// Decompresses `in` into `out` using miniz tinfl API, expecting zlib header.
// Returns number of bytes written to `out`, or 0 on error.
size_t deflate_decompress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    if (!in || !out || out_cap == 0) return 0;
    size_t written = tinfl_decompress_mem_to_mem(out, out_cap, in, in_len, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) return 0;
    return written;
}