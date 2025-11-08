// BWT + MTF + Huffman pipeline implementation (lossless)
#include "../include/compressor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple suffix comparator for BWT (rotation-based)
typedef struct {
    const unsigned char* data;
    long n;
    long idx;
} Suffix;

static int suffix_cmp(const void* a, const void* b) {
    const Suffix* sa = (const Suffix*)a;
    const Suffix* sb = (const Suffix*)b;
    long n = sa->n;
    long ia = sa->idx;
    long ib = sb->idx;
    for (long k = 0; k < n; ++k) {
        unsigned char ca = sa->data[(ia + k) % n];
        unsigned char cb = sb->data[(ib + k) % n];
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    return 0;
}

// Burrows-Wheeler Transform
static int bwt_transform_impl(const unsigned char* in, long n, unsigned char** out, long* out_n, long* primary_index) {
    if (n <= 0) return -1;
    Suffix* sa = (Suffix*)tracked_malloc(sizeof(Suffix) * n);
    if (!sa) return -1;
    for (long i = 0; i < n; ++i) {
        sa[i].data = in;
        sa[i].n = n;
        sa[i].idx = i;
    }
    qsort(sa, n, sizeof(Suffix), suffix_cmp);

    unsigned char* L = (unsigned char*)tracked_malloc(n);
    if (!L) { tracked_free(sa, sizeof(Suffix) * n); return -1; }
    *primary_index = -1;
    for (long i = 0; i < n; ++i) {
        long j = sa[i].idx;
        long prev = (j + n - 1) % n;
        L[i] = in[prev];
        if (j == 0) *primary_index = i;
    }
    if (*primary_index < 0) { tracked_free(L, n); tracked_free(sa, sizeof(Suffix) * n); return -1; }
    *out = L;
    *out_n = n;
    tracked_free(sa, sizeof(Suffix) * n);
    return 0;
}

// Inverse BWT using LF-mapping
static int bwt_inverse_impl(const unsigned char* L, long n, long primary_index, unsigned char** out, long* out_n) {
    if (n <= 0 || primary_index < 0 || primary_index >= n) return -1;
    int count[256];
    memset(count, 0, sizeof(count));
    for (long i = 0; i < n; ++i) count[L[i]]++;

    int start[256];
    int sum = 0;
    for (int c = 0; c < 256; ++c) { start[c] = sum; sum += count[c]; }

    int occ[256];
    memset(occ, 0, sizeof(occ));
    int* T = (int*)tracked_malloc(sizeof(int) * n);
    if (!T) return -1;
    for (long i = 0; i < n; ++i) {
        unsigned char c = L[i];
        T[i] = start[c] + occ[c]++; // LF-mapping
    }

    unsigned char* outbuf = (unsigned char*)tracked_malloc(n);
    if (!outbuf) { tracked_free(T, sizeof(int) * n); return -1; }
    long p = primary_index;
    for (long i = n - 1; i >= 0; --i) { // reconstruct original
        outbuf[i] = L[p];
        p = T[p];
    }

    *out = outbuf;
    *out_n = n;
    tracked_free(T, sizeof(int) * n);
    return 0;
}

// Move-To-Front encode
static int mtf_encode_impl(const unsigned char* in, long n, unsigned char** out, long* out_n) {
    unsigned char list[256];
    for (int i = 0; i < 256; ++i) list[i] = (unsigned char)i;
    unsigned char* enc = (unsigned char*)tracked_malloc(n);
    if (!enc) return -1;
    for (long i = 0; i < n; ++i) {
        unsigned char c = in[i];
        int idx = 0;
        while (list[idx] != c) idx++;
        enc[i] = (unsigned char)idx;
        // move to front
        memmove(&list[1], &list[0], idx);
        list[0] = c;
    }
    *out = enc;
    *out_n = n;
    return 0;
}

// Move-To-Front decode
static int mtf_decode_impl(const unsigned char* in, long n, unsigned char** out, long* out_n) {
    unsigned char list[256];
    for (int i = 0; i < 256; ++i) list[i] = (unsigned char)i;
    unsigned char* dec = (unsigned char*)tracked_malloc(n);
    if (!dec) return -1;
    for (long i = 0; i < n; ++i) {
        int idx = in[i];
        unsigned char c = list[idx];
        dec[i] = c;
        memmove(&list[1], &list[0], idx);
        list[0] = c;
    }
    *out = dec;
    *out_n = n;
    return 0;
}

int bwt_mtf_huffman_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    if (!input || input_size <= 0 || !output || !output_size) return -1;
    unsigned char* bwt;
    long bwt_n;
    long primary_idx;
    if (bwt_transform_impl(input, input_size, &bwt, &bwt_n, &primary_idx) != 0) return -1;

    unsigned char* mtf;
    long mtf_n;
    if (mtf_encode_impl(bwt, bwt_n, &mtf, &mtf_n) != 0) { tracked_free(bwt, bwt_n); return -1; }

    // prepend 4-byte primary index (big-endian) before Huffman encoding
    unsigned char* payload = (unsigned char*)tracked_malloc(mtf_n + 4);
    if (!payload) { tracked_free(bwt, bwt_n); tracked_free(mtf, mtf_n); return -1; }
    payload[0] = (unsigned char)((primary_idx >> 24) & 0xFF);
    payload[1] = (unsigned char)((primary_idx >> 16) & 0xFF);
    payload[2] = (unsigned char)((primary_idx >> 8) & 0xFF);
    payload[3] = (unsigned char)(primary_idx & 0xFF);
    memcpy(payload + 4, mtf, mtf_n);

    unsigned char* huff_out = NULL;
    long huff_n = 0;
    CompResult rc = huffman_compress(payload, mtf_n + 4, &huff_out, &huff_n);
    tracked_free(payload, mtf_n + 4);
    tracked_free(bwt, bwt_n);
    tracked_free(mtf, mtf_n);
    if (rc != COMP_SUCCESS) return -1;

    *output = huff_out;
    *output_size = huff_n;
    return 0;
}

int bwt_mtf_huffman_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    if (!input || input_size <= 0 || !output || !output_size) return -1;
    unsigned char* payload = NULL;
    long payload_n = 0;
    {
        CompResult drc = huffman_decompress_optimized(input, input_size, &payload, &payload_n);
        if (drc != COMP_SUCCESS) return -1;
    }
    if (payload_n < 4) { tracked_free(payload, payload_n); return -1; }
    long primary_idx = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
    unsigned char* mtf_data = payload + 4;
    long mtf_n = payload_n - 4;

    unsigned char* bwt_data = NULL;
    long bwt_n = 0;
    if (mtf_decode_impl(mtf_data, mtf_n, &bwt_data, &bwt_n) != 0) { tracked_free(payload, payload_n); return -1; }

    unsigned char* outbuf = NULL;
    long out_n = 0;
    int rc = bwt_inverse_impl(bwt_data, bwt_n, primary_idx, &outbuf, &out_n);
    tracked_free(payload, payload_n);
    tracked_free(bwt_data, bwt_n);
    if (rc != 0) return -1;

    *output = outbuf;
    *output_size = out_n;
    return 0;
}