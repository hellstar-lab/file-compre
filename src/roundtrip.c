#include "../include/compressor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_files(const char* a, const char* b) {
    unsigned char *buf1 = NULL, *buf2 = NULL; long sz1 = 0, sz2 = 0;
    if (read_file(a, &buf1, &sz1) != 0) return -1;
    if (read_file(b, &buf2, &sz2) != 0) { free(buf1); return -1; }
    int result = 0;
    if (sz1 != sz2 || memcmp(buf1, buf2, sz1) != 0) result = -1;
    free(buf1); free(buf2);
    return result;
}

int main(void) {
    const char* src = "test_text.txt";
    const char* comp = "roundtrip_test.comp";
    const char* out = "roundtrip_test.out";

    CompressionStats stats;
    memset(&stats, 0, sizeof(stats));

    printf("Roundtrip: compress -> decompress -> compare\n");
    printf("Source: %s\n", src);

    if (compress_file_intelligent(src, comp, COMPRESSION_LEVEL_NORMAL, &stats) != 0) {
        printf("Compression failed.\n");
        return 1;
    }
    printf("Compressed: %s (algo=%d, ratio=%.2f%%)\n", comp, stats.algorithm_used, stats.compression_ratio);

    memset(&stats, 0, sizeof(stats));
    if (decompress_file(comp, out, &stats) != 0) {
        printf("Decompression failed.\n");
        return 2;
    }
    printf("Decompressed: %s\n", out);

    if (compare_files(src, out) == 0) {
        printf("✓ Roundtrip success: output matches input byte-for-byte.\n");
        return 0;
    } else {
        printf("✗ Roundtrip mismatch: output differs from input.\n");
        return 3;
    }
}