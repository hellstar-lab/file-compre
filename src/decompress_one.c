#include "../include/compressor.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* in = (argc > 1) ? argv[1] : "data/CLOUD_PBL_PHASE-2.pdf.comp";
    const char* out = (argc > 2) ? argv[2] : "data/CLOUD_PBL_PHASE-2.out";
    CompressionStats stats; memset(&stats, 0, sizeof(stats));
    printf("Decompressing single file: %s\n", in);
    int rc = decompress_file(in, out, &stats);
    printf("Result: %d\n", rc);
    return rc != 0;
}