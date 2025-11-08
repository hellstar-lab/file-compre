// Simple header-codec CLI for MP3 using audio_doc_codec.h
// Supports: -c <in.mp3> to compress, -d <in.comp> to decompress, optional -o <out_dir>
// Uses deflate_wrapper + miniz via audio_doc_codec.h. Ensures roundtrip.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio_doc_codec.h"

static uint8_t* read_file(const char* path, size_t* out_len){
    *out_len = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0){ fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0){ fclose(f); return NULL; }
    rewind(f);
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf){ fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz){ free(buf); return NULL; }
    *out_len = (size_t)sz;
    return buf;
}

static int ensure_dir_suffix(char* out, size_t cap){
    size_t n = strlen(out);
    if (n == 0) return 0;
    char last = out[n-1];
#ifdef _WIN32
    if (last != '\\' && last != '/'){
        if (n+1 >= cap) return 0;
        out[n] = '\\'; out[n+1] = '\0';
    }
#else
    if (last != '/'){
        if (n+1 >= cap) return 0;
        out[n] = '/'; out[n+1] = '\0';
    }
#endif
    return 1;
}

static void basename_no_ext(const char* path, char* out, size_t out_cap){
    const char* p = path;
    const char* last_slash = p;
    for (; *p; ++p){ if (*p=='/' || *p=='\\') last_slash = p+1; }
    const char* base = last_slash;
    const char* dot = NULL;
    for (const char* q = base; *q; ++q){ if (*q == '.') dot = q; }
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (len >= out_cap) len = out_cap - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

static void usage(void){
    fprintf(stderr, "Usage:\n  header_codec.exe -c <in.mp3> [-o <out_dir>]\n  header_codec.exe -d <in.comp> [-o <out_dir>]\n");
}

int main(int argc, char** argv){
    const char* in_path = NULL;
    const char* out_dir = NULL;
    int mode_compress = 0;
    int mode_decompress = 0;
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i], "-c") && i+1<argc){ mode_compress = 1; in_path = argv[++i]; }
        else if (!strcmp(argv[i], "-d") && i+1<argc){ mode_decompress = 1; in_path = argv[++i]; }
        else if (!strcmp(argv[i], "-o") && i+1<argc){ out_dir = argv[++i]; }
        else { usage(); return 2; }
    }
    if ((!mode_compress && !mode_decompress) || !in_path){ usage(); return 2; }
    if (mode_compress && mode_decompress){ fprintf(stderr, "Specify either -c or -d, not both.\n"); return 2; }

    size_t in_len = 0; uint8_t* in_buf = read_file(in_path, &in_len);
    if (!in_buf){ fprintf(stderr, "Failed to read input: %s\n", in_path); return 1; }

    // Prepare output directory prefix
    char out_prefix[1024] = {0};
    if (out_dir){ strncpy(out_prefix, out_dir, sizeof(out_prefix)-1); out_prefix[sizeof(out_prefix)-1]='\0'; ensure_dir_suffix(out_prefix, sizeof(out_prefix)); }

    int rc = 0;
    if (mode_compress){
        size_t out_cap = in_len * 2 + 65536;
        uint8_t* out_buf = (uint8_t*)malloc(out_cap);
        if (!out_buf){ fprintf(stderr, "OOM allocating %zu bytes\n", out_cap); free(in_buf); return 1; }
        size_t cmp_len = mp3_compress(in_buf, in_len, out_buf, out_cap);
        if (!cmp_len){ fprintf(stderr, "Compression failed.\n"); free(out_buf); free(in_buf); return 1; }
        char base[512]; basename_no_ext(in_path, base, sizeof(base));
        char out_path[1536]; snprintf(out_path, sizeof(out_path), "%s%s.comp", out_prefix, base);
        FILE* f = fopen(out_path, "wb");
        if (!f){ fprintf(stderr, "Failed to open output: %s\n", out_path); free(out_buf); free(in_buf); return 1; }
        fwrite(out_buf, 1, cmp_len, f); fclose(f);
        double ratio = (double)cmp_len / (double)in_len;
        printf("Compressed '%s' -> '%s' (%zu -> %zu bytes, ratio %.4f)\n", in_path, out_path, in_len, cmp_len, ratio);
        free(out_buf);
    } else if (mode_decompress){
        size_t out_cap = in_len * 4 + 65536;
        uint8_t* out_buf = (uint8_t*)malloc(out_cap);
        if (!out_buf){ fprintf(stderr, "OOM allocating %zu bytes\n", out_cap); free(in_buf); return 1; }
        size_t dec_len = mp3_decompress(in_buf, in_len, out_buf, out_cap);
        if (!dec_len){ fprintf(stderr, "Decompression failed.\n"); free(out_buf); free(in_buf); return 1; }
        // Output path: if input ends with .comp, strip it; else append .dec.mp3
        const char* in = in_path; size_t n = strlen(in);
        char out_path[1536];
        if (n >= 5 && !strcmp(in + (n-5), ".comp")){
            // strip .comp
            char tmp[1024]; strncpy(tmp, in, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
            tmp[n-5] = '\0';
            snprintf(out_path, sizeof(out_path), "%s%s", out_prefix, tmp + (out_prefix[0] ? 0 : 0));
        } else {
            char base[512]; basename_no_ext(in_path, base, sizeof(base));
            snprintf(out_path, sizeof(out_path), "%s%s.dec.mp3", out_prefix, base);
        }
        FILE* f = fopen(out_path, "wb");
        if (!f){ fprintf(stderr, "Failed to open output: %s\n", out_path); free(out_buf); free(in_buf); return 1; }
        fwrite(out_buf, 1, dec_len, f); fclose(f);
        printf("Decompressed '%s' -> '%s' (%zu bytes)\n", in_path, out_path, dec_len);
        free(out_buf);
    }

    free(in_buf);
    return rc;
}