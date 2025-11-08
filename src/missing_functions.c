#include "../include/compressor.h"

// Provide minimal implementation of lzw_decompress_mfunc referenced by lz77.c
int lzw_decompress_mfunc(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    (void)input; (void)input_size; (void)output; (void)output_size;
    /* FORCE-COMPRESS – raw storage disabled */
    return -1;
}