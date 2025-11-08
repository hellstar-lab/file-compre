#include "../include/compressor.h"

// Minimal BWT+MTF+Huffman stubs; for now, reuse Huffman compressor/decompressor

int bwt_mtf_huffman_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    CompResult rc = huffman_compress(input, input_size, output, output_size);
    return (rc == COMP_SUCCESS) ? 0 : -1;
}

int bwt_mtf_huffman_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    CompResult rc = huffman_decompress(input, input_size, output, output_size);
    return (rc == COMP_SUCCESS) ? 0 : -1;
}