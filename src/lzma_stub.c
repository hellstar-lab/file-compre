// Minimal stub for LZMA when disabled: satisfy linker with no-op implementations
#include <stddef.h>
#include <stdint.h>

size_t lzma_compress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap, uint32_t level) {
    (void)in; (void)in_len; (void)out; (void)out_cap; (void)level;
    return 0;
}

size_t lzma_decompress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    (void)in; (void)in_len; (void)out; (void)out_cap;
    return 0;
}