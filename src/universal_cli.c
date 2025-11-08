#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <direct.h>
#endif

/* Use explicit relative include paths so editors without -Iinclude resolve correctly */
#include "../include/comp_container.h"
#include "../include/zlib_adapter.h"
#include "../include/img_preconditioner.h"
static char* afc_strdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}
#define _strdup afc_strdup

/* Use existing CRC32 implementation from project */
uint32_t CRC32_Calculate(const unsigned char* data, size_t length);

static const size_t CHUNK = 8192;

static const char* get_ext_from_magic(const unsigned char* buf, size_t n) {
    if (n >= 4) {
        if (buf[0] == 0xFF && buf[1] == 0xD8) return "jpg";
        if (buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G') return "png";
        if (buf[0] == 'P' && buf[1] == 'K') return "zip";
        if (buf[0] == '%' && buf[1] == 'P' && buf[2] == 'D' && buf[3] == 'F') return "pdf";
        if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F') return "gif";
        if (buf[0] == 'B' && buf[1] == 'M') return "bmp";
        if (buf[0] == 'R' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == 'F') return "wav";
    }
    return "bin";
}

static const char* get_ext_from_path(const char* path) {
    const char* dot = strrchr(path, '.');
    if (dot && *(dot + 1) != '\0') {
        return dot + 1;
    }
    return NULL;
}

static char* make_out_path(const char* in_path, const char* new_ext) {
    size_t len = strlen(in_path);
    /* If path ends with .comp, strip it and return base */
    if (len >= 5 && strcmp(in_path + (len - 5), ".comp") == 0) {
        char* out = (char*)malloc(len - 5 + 1);
        if (!out) return NULL;
        memcpy(out, in_path, len - 5);
        out[len - 5] = '\0';
        return out;
    }
    /* Otherwise, replace extension with new_ext */
    char* out = (char*)malloc(len + 16);
    if (!out) return NULL;
    strcpy(out, in_path);
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    strcat(out, ".");
    strcat(out, new_ext);
    return out;
}

static char* make_comp_path(const char* in_path) {
    size_t len = strlen(in_path);
    char* out = (char*)malloc(len + 6);
    if (!out) return NULL;
    strcpy(out, in_path);
    strcat(out, ".comp");
    return out;
}

/* Cross-platform helpers for output directory support */
static char path_sep(void) {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

static char* basename_dup(const char* path) {
    if (!path) return NULL;
    const char* last_slash = strrchr(path, '/');
    const char* last_backslash = strrchr(path, '\\');
    const char* base = path;
    if (last_slash && last_backslash) {
        base = (last_slash > last_backslash) ? last_slash + 1 : last_backslash + 1;
    } else if (last_slash) {
        base = last_slash + 1;
    } else if (last_backslash) {
        base = last_backslash + 1;
    }
    return _strdup(base);
}

static char* join_dir_file(const char* dir, const char* filename) {
    if (!dir || !filename) return NULL;
    size_t dlen = strlen(dir);
    size_t flen = strlen(filename);
    int need_sep = (dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\');
    char sep = path_sep();
    size_t total = dlen + (need_sep ? 1 : 0) + flen + 1;
    char* out = (char*)malloc(total);
    if (!out) return NULL;
    strcpy(out, dir);
    if (need_sep) {
        size_t pos = strlen(out);
        out[pos] = sep;
        out[pos + 1] = '\0';
    }
    strcat(out, filename);
    return out;
}

static void ensure_dir_exists(const char* dir) {
    if (!dir || !*dir) return;
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0777);
#endif
}

static char* make_comp_path_in_dir(const char* in_path, const char* out_dir) {
    char* base = basename_dup(in_path);
    if (!base) return NULL;
    size_t blen = strlen(base);
    char* compname = (char*)malloc(blen + 6);
    if (!compname) { free(base); return NULL; }
    strcpy(compname, base);
    strcat(compname, ".comp");
    char* out = join_dir_file(out_dir, compname);
    free(base);
    free(compname);
    return out;
}

static char* make_out_path_in_dir(const char* comp_path, const char* new_ext, const char* out_dir) {
    char* base = basename_dup(comp_path);
    if (!base) return NULL;
    size_t blen = strlen(base);
    if (blen >= 5 && strcmp(base + (blen - 5), ".comp") == 0) {
        base[blen - 5] = '\0';
    } else if (new_ext && *new_ext) {
        /* If not a .comp file, try to ensure extension */
        char* dot = strrchr(base, '.');
        if (!dot) {
            char* with_ext = (char*)malloc(strlen(base) + 1 + strlen(new_ext) + 1);
            if (!with_ext) { free(base); return NULL; }
            strcpy(with_ext, base);
            strcat(with_ext, ".");
            strcat(with_ext, new_ext);
            free(base);
            base = with_ext;
        }
    }
    char* out = join_dir_file(out_dir, base);
    free(base);
    return out;
}

static int compress_file(const char* input_path, const char* out_dir, int use_zlib) {
    FILE* fi = fopen(input_path, "rb");
    if (!fi) { fprintf(stderr, "Failed to open %s\n", input_path); return -1; }

    /* Read entire file into memory for simplicity; could stream if needed */
    fseek(fi, 0, SEEK_END);
    long sz = ftell(fi);
    if (sz < 0) { fclose(fi); fprintf(stderr, "ftell failed\n"); return -1; }
    fseek(fi, 0, SEEK_SET);
    unsigned char* inbuf = (unsigned char*)malloc((size_t)sz);
    if (!inbuf) { fclose(fi); fprintf(stderr, "Memory alloc failed\n"); return -1; }
    if (fread(inbuf, 1, (size_t)sz, fi) != (size_t)sz) { free(inbuf); fclose(fi); fprintf(stderr, "Read failed\n"); return -1; }
    fclose(fi);

    const char* ext = get_ext_from_path(input_path);
    if (!ext) {
        ext = get_ext_from_magic(inbuf, (size_t)sz);
    }
    uint32_t crc = CRC32_Calculate(inbuf, (size_t)sz);

    /* Try lossless BMP preconditioning (SUB filter) to target ~20%+ ratio */
    const unsigned char* comp_input = inbuf;
    size_t comp_input_size = (size_t)sz;
    unsigned char* pre_buf = NULL;
    size_t pre_size = 0;
    bmp_info_t bmp;
    int is_bmp = 0;
    if (ext && (strcasecmp(ext, "bmp") == 0)) {
        is_bmp = bmp_detect_24(inbuf, (size_t)sz < 256 ? (size_t)sz : 256, &bmp);
    }
    if (is_bmp) {
        /* Build IMGF payload: [prelude][header][encoded pixels] */
        size_t pixels_size = (size_t)bmp.row_stride * (size_t)bmp.height;
        size_t header_size = (size_t)bmp.header_size;
        size_t payload_cap = sizeof(imgf_prelude_t) + header_size + pixels_size;
        pre_buf = (unsigned char*)malloc(payload_cap);
        if (!pre_buf) { free(inbuf); fprintf(stderr, "Memory alloc failed\n"); return -1; }
        imgf_prelude_t prel;
        memcpy(prel.magic, "IMGF", 4);
        prel.version = 1;
        prel.transform = 1; /* SUB */
        prel.reserved16 = 0;
        prel.header_size = bmp.header_size;
        prel.width = bmp.width;
        prel.height = bmp.height;
        prel.bpp = bmp.bpp;
        prel.reserved = 0;
        prel.row_stride = bmp.row_stride;
        memcpy(pre_buf, &prel, sizeof(prel));
        /* Copy original header */
        memcpy(pre_buf + sizeof(prel), inbuf, header_size);
        /* Encode pixels */
        const unsigned char* src_pixels = inbuf + header_size;
        unsigned char* dst_pixels = pre_buf + sizeof(prel) + header_size;
        if (!bmp_sub_encode(src_pixels, dst_pixels, &bmp)) {
            free(pre_buf); pre_buf = NULL;
        } else {
            comp_input = pre_buf;
            comp_input_size = payload_cap;
            pre_size = payload_cap;
        }
    }

    /* Output buffer allocation: use a generous bound to avoid zlib/miniz BUF errors */
    size_t out_capacity = comp_input_size * 2 + 65536;
    unsigned char* outbuf = (unsigned char*)malloc(out_capacity);
    if (!outbuf) { if (pre_buf) free(pre_buf); free(inbuf); fprintf(stderr, "Memory alloc failed\n"); return -1; }

    size_t comp_size = 0;
    int rc = 0;
    comp_algo_t algo = COMP_ALGO_ZLIB;
    rc = za_compress_buffer(comp_input, comp_input_size, outbuf, out_capacity, &comp_size);
    if (rc != 0) { free(outbuf); if (pre_buf) free(pre_buf); free(inbuf); fprintf(stderr, "Compression failed\n"); return -1; }

    comp_header_t hdr;
    comp_fill_header(&hdr, algo, (uint64_t)sz, (uint64_t)comp_size, crc, ext);

    char* outpath = NULL;
    if (out_dir && *out_dir) {
        ensure_dir_exists(out_dir);
        outpath = make_comp_path_in_dir(input_path, out_dir);
    } else {
        /* Default compression output directory */
        const char* default_comp_dir = "output";
        ensure_dir_exists(default_comp_dir);
        outpath = make_comp_path_in_dir(input_path, default_comp_dir);
    }
    if (!outpath) { free(outbuf); free(inbuf); fprintf(stderr, "Path alloc failed\n"); return -1; }
    FILE* fo = fopen(outpath, "wb");
    if (!fo) { free(outpath); free(outbuf); free(inbuf); fprintf(stderr, "Failed to open output\n"); return -1; }

    if (!comp_write_header(fo, &hdr)) { fclose(fo); free(outpath); free(outbuf); free(inbuf); fprintf(stderr, "Header write failed\n"); return -1; }
    if (fwrite(outbuf, 1, comp_size, fo) != comp_size) { fclose(fo); free(outpath); free(outbuf); free(inbuf); fprintf(stderr, "Payload write failed\n"); return -1; }
    fclose(fo);
    fprintf(stdout, "[SUCCESS] Compressed %s -> %s (%zu -> %zu bytes)\n", input_path, outpath, (size_t)sz, comp_size);
    // Emit standardized lines for GUI parsers
    fprintf(stdout, "Output: %s\n", outpath);
    fprintf(stdout, "Progress: 100\n");

    free(outpath);
    free(outbuf);
    if (pre_buf) free(pre_buf);
    free(inbuf);
    return 0;
}

static int decompress_file(const char* comp_path, const char* out_dir) {
    /* Enforce that decompression only operates on .comp files inside the output/ directory */
    if (!comp_path || *comp_path == '\0') { fprintf(stderr, "Invalid path\n"); return -1; }
    size_t cplen = strlen(comp_path);
    if (cplen < 5 || strcmp(comp_path + (cplen - 5), ".comp") != 0) {
        fprintf(stderr, "Refusing to decompress: input must be a .comp file\n");
        return -1;
    }
    /* Accept either relative paths starting with output/ or absolute paths containing /output/ segment */
    int in_output_dir = 0;
    if (strncmp(comp_path, "output/", 7) == 0 || strncmp(comp_path, "output\\", 7) == 0) {
        in_output_dir = 1;
    } else {
        const char* pos_fwd = strstr(comp_path, "/output/");
        const char* pos_bwd = strstr(comp_path, "\\output\\");
        if (pos_fwd || pos_bwd) {
            in_output_dir = 1;
        }
    }
    if (!in_output_dir) {
        fprintf(stderr, "Refusing to decompress: input must reside under 'output/' directory\n");
        return -1;
    }

    FILE* fi = fopen(comp_path, "rb");
    if (!fi) { fprintf(stderr, "Failed to open %s\n", comp_path); return -1; }
    comp_header_t hdr;
    if (!comp_read_header(fi, &hdr) || !comp_validate_header(&hdr)) {
        fclose(fi); fprintf(stderr, "Invalid COMP header\n"); return -1; }

    size_t payload_size = (size_t)hdr.compressed_size;
    unsigned char* inbuf = (unsigned char*)malloc(payload_size);
    if (!inbuf) { fclose(fi); fprintf(stderr, "Memory alloc failed\n"); return -1; }
    if (fread(inbuf, 1, payload_size, fi) != payload_size) { free(inbuf); fclose(fi); fprintf(stderr, "Payload read failed\n"); return -1; }
    fclose(fi);

    size_t out_capacity = (size_t)hdr.original_size + 65536; /* allow for IMGF prelude */
    unsigned char* outbuf = (unsigned char*)malloc(out_capacity);
    if (!outbuf) { free(inbuf); fprintf(stderr, "Memory alloc failed\n"); return -1; }

    size_t out_size = 0;
    int rc = za_decompress_buffer(inbuf, payload_size, outbuf, out_capacity, &out_size);
    if (rc != 0) {
        free(outbuf); free(inbuf); fprintf(stderr, "Decompression failed\n"); return -1; }

    /* If IMGF prelude present, reverse transform to rebuild original bytes */
    unsigned char* finalbuf = NULL;
    size_t final_size = out_size;
    if (out_size >= sizeof(imgf_prelude_t) && memcmp(outbuf, "IMGF", 4) == 0) {
        imgf_prelude_t prel;
        memcpy(&prel, outbuf, sizeof(prel));
        if (prel.version == 1 && prel.transform == 1) {
            bmp_info_t info;
            info.header_size = prel.header_size;
            info.pixel_offset = prel.header_size;
            info.width = prel.width;
            info.height = prel.height;
            info.bpp = prel.bpp;
            info.row_stride = prel.row_stride;
            size_t pixels_size = (size_t)info.row_stride * (size_t)info.height;
            size_t header_size = (size_t)info.header_size;
            if (out_size >= sizeof(prel) + header_size + pixels_size) {
                final_size = header_size + pixels_size;
                finalbuf = (unsigned char*)malloc(final_size);
                if (!finalbuf) { free(outbuf); free(inbuf); fprintf(stderr, "Memory alloc failed\n"); return -1; }
                /* Copy original header */
                memcpy(finalbuf, outbuf + sizeof(prel), header_size);
                /* Decode pixels */
                const unsigned char* enc_pixels = outbuf + sizeof(prel) + header_size;
                unsigned char* dec_pixels = finalbuf + header_size;
                if (!bmp_sub_decode(enc_pixels, dec_pixels, &info)) {
                    free(finalbuf); finalbuf = NULL; final_size = out_size;
                }
            }
        }
    }
    const unsigned char* verify_buf = finalbuf ? finalbuf : outbuf;
    size_t verify_size = finalbuf ? final_size : out_size;
    uint32_t crc = CRC32_Calculate(verify_buf, verify_size);
    if (crc != hdr.crc32) {
        if (finalbuf) free(finalbuf);
        free(outbuf); free(inbuf); fprintf(stderr, "CRC mismatch: expected %u got %u\n", hdr.crc32, crc); return -1; }

    char* outpath = NULL;
    if (out_dir && *out_dir) {
        ensure_dir_exists(out_dir);
        outpath = make_out_path_in_dir(comp_path, hdr.ext, out_dir);
    } else {
        /* Default decompression output directory */
        const char* default_decomp_dir = "decompressed";
        ensure_dir_exists(default_decomp_dir);
        outpath = make_out_path_in_dir(comp_path, hdr.ext, default_decomp_dir);
    }
    if (!outpath) { free(outbuf); free(inbuf); fprintf(stderr, "Path alloc failed\n"); return -1; }
    FILE* fo = fopen(outpath, "wb");
    if (!fo) { free(outpath); free(outbuf); free(inbuf); fprintf(stderr, "Failed to open output\n"); return -1; }
    const unsigned char* write_buf = finalbuf ? finalbuf : outbuf;
    size_t write_size = finalbuf ? final_size : out_size;
    if (fwrite(write_buf, 1, write_size, fo) != write_size) {
        fclose(fo); free(outpath); free(outbuf); free(inbuf); fprintf(stderr, "Write failed\n"); return -1; }
    fclose(fo);
    fprintf(stdout, "[SUCCESS] Decompressed %s -> %s (Verified)\n", comp_path, outpath);
    // Emit standardized lines for GUI parsers
    fprintf(stdout, "Output: %s\n", outpath);
    fprintf(stdout, "Progress: 100\n");

    free(outpath);
    if (finalbuf) free(finalbuf);
    free(outbuf);
    free(inbuf);
    return 0;
}

static void usage(void) {
    printf("Usage:\n");
    printf("  universal -c <file> [-o <dir>]        # compress single file (default: output/)\n");
    printf("  universal -d <file.comp> [-o <dir>]   # decompress file (default: decompressed/)\n");
    printf("  universal --zlib -c <file> [-o <dir>] # compress using zlib (if available)\n");
    printf("Note: Wrap paths containing spaces in quotes. Missing output dirs are created.\n");
    printf("Restriction: Decompression only accepts .comp files inside the 'output/' directory.\n");
}

int main(int argc, char** argv) {
    int use_zlib = 0;
    int argi = 1;

    /* Debug print argv to diagnose invocation issues */
    fprintf(stderr, "[dbg] argc=%d\n", argc);
    for (int i = 0; i < argc; ++i) {
        fprintf(stderr, "[dbg] argv[%d]=%s\n", i, argv[i]);
    }

    /* Handle optional --zlib flag */
    if (argc >= 2 && strcmp(argv[1], "--zlib") == 0) {
        use_zlib = 1;
        argi = 2;
        if (argc < 4) { usage(); return 1; }
    } else {
        if (argc < 3) { usage(); return 1; }
    }

    const char* mode = argv[argi++];
    const char* path = argv[argi];
    const char* out_dir = NULL;

    /* Parse optional -o <dir> after the main path */
    for (int i = argi + 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            out_dir = argv[i + 1];
            break;
        }
    }

    /* Sanitize quoted paths passed literally by shells (e.g., PowerShell --%): */
    char* path_sanitized = NULL;
    char* outdir_sanitized = NULL;
    if (path) {
        size_t plen = strlen(path);
        if (plen >= 2 && ((path[0] == '"' && path[plen - 1] == '"') || (path[0] == '\'' && path[plen - 1] == '\''))) {
            path_sanitized = (char*)malloc(plen - 1);
            if (!path_sanitized) { fprintf(stderr, "Memory alloc failed\n"); return 1; }
            memcpy(path_sanitized, path + 1, plen - 2);
            path_sanitized[plen - 2] = '\0';
        } else {
            path_sanitized = _strdup(path);
            if (!path_sanitized) { fprintf(stderr, "Memory alloc failed\n"); return 1; }
        }
        /* Strip trailing semicolons possibly passed by shell stop-parsing */
        size_t slen = strlen(path_sanitized);
        if (slen > 0 && path_sanitized[slen - 1] == ';') {
            path_sanitized[slen - 1] = '\0';
        }
    }

    if (out_dir) {
        size_t olen = strlen(out_dir);
        if (olen >= 2 && ((out_dir[0] == '"' && out_dir[olen - 1] == '"') || (out_dir[0] == '\'' && out_dir[olen - 1] == '\''))) {
            outdir_sanitized = (char*)malloc(olen - 1);
            if (!outdir_sanitized) { fprintf(stderr, "Memory alloc failed\n"); if (path_sanitized) free(path_sanitized); return 1; }
            memcpy(outdir_sanitized, out_dir + 1, olen - 2);
            outdir_sanitized[olen - 2] = '\0';
        } else {
            outdir_sanitized = _strdup(out_dir);
            if (!outdir_sanitized) { fprintf(stderr, "Memory alloc failed\n"); if (path_sanitized) free(path_sanitized); return 1; }
        }
        /* Strip trailing semicolons */
        size_t oslen = strlen(outdir_sanitized);
        if (oslen > 0 && outdir_sanitized[oslen - 1] == ';') {
            outdir_sanitized[oslen - 1] = '\0';
        }
    }

    int ret = 1;
    if (strcmp(mode, "-c") == 0) {
        ret = compress_file(path_sanitized ? path_sanitized : path, outdir_sanitized ? outdir_sanitized : out_dir, use_zlib);
    } else if (strcmp(mode, "-d") == 0) {
        ret = decompress_file(path_sanitized ? path_sanitized : path, outdir_sanitized ? outdir_sanitized : out_dir);
    } else {
        usage();
        ret = 1;
    }

    if (path_sanitized) free(path_sanitized);
    if (outdir_sanitized) free(outdir_sanitized);
    return ret;
}
