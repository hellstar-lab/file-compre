#include "../include/compressor.h"

// FORCE-COMPRESS: LZW stub disabled; do not allow raw passthrough
int lzw_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    (void)input; (void)input_size; (void)output; (void)output_size;
    return -1;
}