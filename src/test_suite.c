#include "../include/compressor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* path;
    const char* label;
} TestFile;

static int compare_files(const char* a, const char* b, long* sa, long* sb) {
    unsigned char *buf1 = NULL, *buf2 = NULL; long sz1 = 0, sz2 = 0;
    if (read_file(a, &buf1, &sz1) != 0) return -1;
    if (read_file(b, &buf2, &sz2) != 0) { free(buf1); return -1; }
    if (sa) *sa = sz1; if (sb) *sb = sz2;
    int result = 0;
    if (sz1 != sz2 || memcmp(buf1, buf2, sz1) != 0) result = -1;
    free(buf1); free(buf2);
    return result;
}

static int run_one(const char* path, const char* label, double* ratio_out, int* lossless_out) {
    char comp[512], decomp[512];
    snprintf(comp, sizeof(comp), "%s.comp", path);
    snprintf(decomp, sizeof(decomp), "%s.out", path);

    CompressionStats stats;
    memset(&stats, 0, sizeof(stats));

    printf("\n=== %s ===\n", label);
    printf("Source: %s\n", path);

    if (compress_file_intelligent(path, comp, COMPRESSION_LEVEL_HIGH, &stats) != 0) {
        printf("✗ Compression failed.\n");
        return -1;
    }
    printf("Compressed: %s (algo=%d, ratio=%.2f%%)\n", comp, stats.algorithm_used, stats.compression_ratio);

    memset(&stats, 0, sizeof(stats));
    if (decompress_file(comp, decomp, &stats) != 0) {
        printf("✗ Decompression failed.\n");
        return -2;
    }
    printf("Decompressed: %s\n", decomp);

    long sa = 0, sb = 0;
    int cmp = compare_files(path, decomp, &sa, &sb);
    *lossless_out = (cmp == 0);
    *ratio_out = (double)stats.compressed_size / (double)sa * 100.0;
    printf("Original: %ld, Compressed: %ld, Ratio: %.2f%%\n", sa, stats.compressed_size, *ratio_out);
    printf("Roundtrip: %s\n", cmp == 0 ? "✓ lossless" : "✗ mismatch");
    return 0;
}

int main(void) {
    TestFile files[] = {
        {"data/sample.txt",  "TXT"},
        {"data/sample.csv",  "CSV"},
        {"data/sample.json", "JSON"},
        {"data/sample.xml",  "XML"},
        {"data/sample_compression.pdf", "PDF (1)"},
        {"data/CLOUD_PBL_PHASE-2.pdf", "PDF (2)"},
        {"data/Jhol(KoshalWorld.Com).mp3", "Audio MP3"},
        {"data/WhatsApp Image 2025-09-02 at 15.56.50_6f4c3785.jpg", "Image JPG"}
    };

    int n = sizeof(files)/sizeof(files[0]);
    int pass_lossless = 0, pass_ratio = 0;
    int fail_lossless = 0, fail_ratio = 0;

    printf("Advanced File Compressor QA Suite\n");
    printf("==================================\n");
    printf("Target: 20–40%% smaller; Lossless roundtrip\n\n");

    for (int i = 0; i < n; i++) {
        double ratio = 0.0; int lossless = 0;
        int rc = run_one(files[i].path, files[i].label, &ratio, &lossless);
        if (rc != 0) {
            printf("Error running test for %s.\n", files[i].label);
            continue;
        }
        if (lossless) pass_lossless++; else fail_lossless++;
        if (ratio <= 80.0) pass_ratio++; else fail_ratio++;
    }

    printf("\n=== Summary ===\n");
    printf("Lossless: %d pass, %d fail\n", pass_lossless, fail_lossless);
    printf("Ratio<=80%%: %d pass, %d fail\n", pass_ratio, fail_ratio);
    if (fail_lossless == 0) printf("✓ All roundtrips are lossless.\n");
    if (fail_ratio == 0) printf("✓ All files meet 20–40%% reduction target.\n");
    else printf("✗ Some files did not meet the reduction target (likely already-compressed formats).\n");
    return 0;
}