#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Forward declarations for DEFLATE via miniz wrapper (enabled when MINIZ_ENABLED=1)
#ifdef USE_MINIZ
size_t deflate_compress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap, int level);
size_t deflate_decompress(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap);
#endif

// Simple utility: safe append into out buffer
static int buf_append(uint8_t* out, size_t out_cap, size_t* off, const void* data, size_t len) {
    if (!out || !off) return 0;
    if (*off + len > out_cap) return 0;
    memcpy(out + *off, data, len);
    *off += len;
    return 1;
}

static int starts_with(const uint8_t* s, size_t len, const char* prefix) {
    size_t n = strlen(prefix);
    if (len < n) return 0;
    return (memcmp(s, prefix, n) == 0);
}

// Find substring (byte-wise) within [data..data+len)
static const uint8_t* find_sub(const uint8_t* data, size_t len, const char* pat) {
    size_t n = strlen(pat);
    if (n == 0 || len < n) return NULL;
    for (size_t i = 0; i + n <= len; i++) {
        if (memcmp(data + i, pat, n) == 0) return data + i;
    }
    return NULL;
}

// Parse an integer value from ASCII, skipping non-digits, returns -1 on failure
static int parse_int_token(const uint8_t* s, size_t len) {
    size_t i = 0;
    while (i < len && (s[i] < '0' || s[i] > '9')) i++;
    if (i >= len) return -1;
    int v = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        i++;
    }
    return v;
}

// PNG-style Paeth predictor filter per row
static void apply_paeth_filter(const uint8_t* in, uint8_t* out, int row_bytes, int bpp, const uint8_t* prev_row) {
    out[0] = 4; // Paeth filter type
    for (int x = 0; x < row_bytes; x++) {
        int a = (x - bpp >= 0) ? in[x - bpp] : 0;            // left
        int b = prev_row ? prev_row[x] : 0;                  // up
        int c = prev_row ? ((x - bpp >= 0) ? prev_row[x - bpp] : 0) : 0; // up-left
        int p = a + b - c;
        int pa = abs(p - a); int pb = abs(p - b); int pc = abs(p - c);
        int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
        int raw = in[x];
        int filt = (raw - pr) & 0xFF;
        out[1 + x] = (uint8_t)filt;
    }
}

// Attempt to detect image params in dictionary and decide if high-PPI (approximation)
static int should_filter_image(const uint8_t* dict, size_t dict_len, int* width, int* height, int* bits, int* colors) {
    const uint8_t* subtype = find_sub(dict, dict_len, "/Subtype");
    if (!subtype) return 0;
    const uint8_t* img = find_sub(subtype, dict_len - (size_t)(subtype - dict), "/Image");
    if (!img) return 0;
    const uint8_t* cs = find_sub(dict, dict_len, "/ColorSpace");
    int c = 1;
    if (cs) {
        const uint8_t* gray = find_sub(cs, dict_len - (size_t)(cs - dict), "/DeviceGray");
        const uint8_t* rgb = find_sub(cs, dict_len - (size_t)(cs - dict), "/DeviceRGB");
        if (rgb) c = 3; else c = 1;
    }
    const uint8_t* wpos = find_sub(dict, dict_len, "/Width");
    const uint8_t* hpos = find_sub(dict, dict_len, "/Height");
    const uint8_t* bpos = find_sub(dict, dict_len, "/BitsPerComponent");
    int w = wpos ? parse_int_token(wpos, dict_len - (size_t)(wpos - dict)) : -1;
    int h = hpos ? parse_int_token(hpos, dict_len - (size_t)(hpos - dict)) : -1;
    int b = bpos ? parse_int_token(bpos, dict_len - (size_t)(bpos - dict)) : 8;
    if (width) *width = (w > 0 ? w : 0);
    if (height) *height = (h > 0 ? h : 0);
    if (bits) *bits = (b > 0 ? b : 8);
    if (colors) *colors = c;
    // Approximation: treat large images as high-PPI
    if (w > 512 || h > 512) return 1;
    return 0;
}

// Rebuild a minimal classic xref table and trailer from object offsets
static int write_xref_and_trailer(uint8_t* out, size_t out_cap, size_t* off,
                                  size_t* obj_offsets, int obj_count,
                                  const uint8_t* original_trailer, size_t trailer_len) {
    const char* xref = "xref\n";
    const char* subsec = "0 ";
    char buf[64];
    if (!buf_append(out, out_cap, off, xref, strlen(xref))) return 0;
    // write "0 N\n"
    int n = obj_count + 1; // include object 0 free
    int nlen = snprintf(buf, sizeof(buf), "0 %d\n", n);
    if (!buf_append(out, out_cap, off, buf, (size_t)nlen)) return 0;
    // object 0 free entry
    if (!buf_append(out, out_cap, off, "0000000000 65535 f \n", 20)) return 0;
    for (int i = 0; i < obj_count; i++) {
        // 10-digit offset padded with leading zeros
        char line[64];
        unsigned long long off10 = (unsigned long long)obj_offsets[i];
        int llen = snprintf(line, sizeof(line), "%010llu 00000 n \n", off10);
        if (!buf_append(out, out_cap, off, line, (size_t)llen)) return 0;
    }
    // Write trailer: copy original but replace /Size
    if (!buf_append(out, out_cap, off, "trailer\n", 8)) return 0;
    // Attempt to preserve keys except /Size
    const uint8_t* sz = find_sub(original_trailer, trailer_len, "/Size");
    size_t dict_start = 0, dict_end = trailer_len;
    if (sz) {
        // Find end of size token (until next '/' or '>>')
        size_t idx = (size_t)(sz - original_trailer);
        size_t end = idx;
        while (end < trailer_len && original_trailer[end] != '/' && original_trailer[end] != '>') end++;
        // Write before /Size
        if (!buf_append(out, out_cap, off, original_trailer + dict_start, idx - dict_start)) return 0;
        // Inject new /Size
        int slen = snprintf(buf, sizeof(buf), "/Size %d ", n);
        if (!buf_append(out, out_cap, off, buf, (size_t)slen)) return 0;
        // Append rest
        if (!buf_append(out, out_cap, off, original_trailer + end, trailer_len - end)) return 0;
    } else {
        // No /Size found; wrap original as-is and add /Size
        if (!buf_append(out, out_cap, off, original_trailer, trailer_len)) return 0;
    }
    if (!buf_append(out, out_cap, off, "\nstartxref\n", 11)) return 0;
    // startxref value: current offset
    unsigned long long sx = (unsigned long long)(*off);
    int sxlen = snprintf(buf, sizeof(buf), "%llu\n%%EOF\n", sx);
    return buf_append(out, out_cap, off, buf, (size_t)sxlen);
}

// Core reflate: recompress /FlateDecode streams, optionally PNG-filter large grayscale/RGB images, sort stream objects by size
size_t pdf_compress(const uint8_t* pdf, size_t pdf_len, uint8_t* out, size_t out_cap) {
    if (!pdf || pdf_len < 8 || !out || out_cap == 0) return 0;
    if (!starts_with(pdf, pdf_len, "%PDF-")) return 0;

    // Collect objects with their recompressed streams and non-streams
    typedef struct ObjRec {
        int num;
        int gen;
        const uint8_t* dict_start;
        size_t dict_len;
        const uint8_t* stream_start;
        size_t stream_len;
        uint8_t* new_stream;
        size_t new_stream_len;
        int has_flate;
    } ObjRec;

    ObjRec* objs = NULL;
    int obj_cap = 64, obj_cnt = 0;
    objs = (ObjRec*)malloc(sizeof(ObjRec) * obj_cap);
    if (!objs) return 0;
    memset(objs, 0, sizeof(ObjRec) * obj_cap);

    // Find trailer for later reuse
    const uint8_t* trailer = find_sub(pdf, pdf_len, "trailer");
    const uint8_t* startxref = trailer ? find_sub(trailer, pdf_len - (size_t)(trailer - pdf), "startxref") : NULL;
    size_t trailer_len = 0;
    if (trailer && startxref && startxref > trailer) {
        trailer_len = (size_t)(startxref - trailer);
    }

    // Scan objects: pattern "<num> <gen> obj" ... "endobj"
    size_t i = 0;
    while (i + 6 < pdf_len) {
        // Look for "obj"
        const uint8_t* objkw = find_sub(pdf + i, pdf_len - i, " obj");
        if (!objkw) break;
        // backtrack to parse num and gen
        size_t kwpos = (size_t)(objkw - pdf);
        size_t start = kwpos;
        // find start of line
        while (start > 0 && pdf[start - 1] != '\n' && pdf[start - 1] != '\r') start--;
        int num = parse_int_token(pdf + start, kwpos - start);
        // find second number \n separated
        const uint8_t* after_num = pdf + start;
        // skip first integer
        while (after_num < pdf + kwpos && *after_num >= '0' && *after_num <= '9') after_num++;
        int gen = parse_int_token(after_num, (size_t)(pdf + kwpos - after_num));

        // find endobj
        const uint8_t* endobj = find_sub(objkw, pdf_len - kwpos, "endobj");
        if (!endobj) break;
        size_t obj_end = (size_t)(endobj - pdf);

        // find stream
        const uint8_t* stream_kw = find_sub(objkw, (size_t)(endobj - objkw), "stream");
        const uint8_t* endstream_kw = NULL;
        const uint8_t* dict_start = pdf + kwpos + 4; // after " obj"
        if (stream_kw) {
            endstream_kw = find_sub(stream_kw, (size_t)(endobj - stream_kw), "endstream");
        }
        size_t dict_len = (size_t)(stream_kw ? (stream_kw - dict_start) : (endobj - dict_start));
        const uint8_t* stream_start = stream_kw ? (stream_kw + 6) : NULL; // right after "stream"
        // Skip possible CRLF after stream
        if (stream_start && stream_start < pdf + pdf_len) {
            if (*stream_start == '\r') stream_start++;
            if (*stream_start == '\n') stream_start++;
        }
        size_t stream_len = 0;
        if (stream_start && endstream_kw) {
            // exclude possible CRLF before endstream
            const uint8_t* s_end = endstream_kw;
            if ((s_end > stream_start) && (*(s_end - 1) == '\n')) s_end--;
            if ((s_end > stream_start) && (*(s_end - 1) == '\r')) s_end--;
            stream_len = (size_t)(s_end - stream_start);
        }

        // Expand object list if needed
        if (obj_cnt >= obj_cap) {
            int new_cap = obj_cap * 2;
            ObjRec* tmp = (ObjRec*)realloc(objs, sizeof(ObjRec) * new_cap);
            if (!tmp) { free(objs); return 0; }
            objs = tmp; obj_cap = new_cap;
        }
        ObjRec* rec = &objs[obj_cnt++];
        memset(rec, 0, sizeof(*rec));
        rec->num = num; rec->gen = gen;
        rec->dict_start = dict_start; rec->dict_len = dict_len;
        rec->stream_start = stream_start; rec->stream_len = stream_len;
        // Check Filter
        int has_flate = 0;
        if (dict_start && dict_len > 0) {
            const uint8_t* fpos = find_sub(dict_start, dict_len, "/Filter");
            if (fpos) {
                const uint8_t* fl = find_sub(fpos, dict_len - (size_t)(fpos - dict_start), "/FlateDecode");
                if (fl) has_flate = 1;
            }
        }
        rec->has_flate = has_flate;

        // If flate stream, recompress with level 9; apply PNG filter for large grayscale/RGB
        rec->new_stream = NULL; rec->new_stream_len = 0;
#ifdef USE_MINIZ
        if (has_flate && stream_start && stream_len > 0) {
            // Try decompress with generous cap (8x compressed size)
            size_t raw_cap = stream_len * 8 + 65536;
            uint8_t* raw = (uint8_t*)malloc(raw_cap);
            if (!raw) { free(objs); return 0; }
            size_t raw_len = deflate_decompress(stream_start, stream_len, raw, raw_cap);
            if (raw_len == 0) {
                // If failed, keep original compressed stream
                rec->new_stream = (uint8_t*)malloc(stream_len);
                if (!rec->new_stream) { free(raw); free(objs); return 0; }
                memcpy(rec->new_stream, stream_start, stream_len);
                rec->new_stream_len = stream_len;
                free(raw);
            } else {
                // Decide if image and high-PPI; if so, apply per-row Paeth filter
                int w=0,h=0,b=8,c=1;
                int do_filter = should_filter_image(dict_start, dict_len, &w, &h, &b, &c);
                uint8_t* to_comp = raw;
                size_t to_comp_len = raw_len;
                uint8_t* filtered = NULL;
                if (do_filter && w>0 && h>0 && (b==8 || b==16) && (c==1 || c==3)) {
                    int bpp = (b==8 ? 1 : 2) * c;
                    size_t row_bytes = (size_t)w * (size_t)bpp;
                    size_t need = ((size_t)h) * (row_bytes + 1);
                    if (need < raw_len && need > 0) {
                        filtered = (uint8_t*)malloc(need);
                        if (filtered) {
                            const uint8_t* prev = NULL;
                            size_t src_off = 0, dst_off = 0;
                            for (int r = 0; r < h && src_off + row_bytes <= raw_len; r++) {
                                apply_paeth_filter(raw + src_off, filtered + dst_off, (int)row_bytes, bpp, prev);
                                prev = raw + src_off;
                                src_off += row_bytes;
                                dst_off += (row_bytes + 1);
                            }
                            to_comp = filtered;
                            to_comp_len = need;
                        }
                    }
                }
                // Recompress with level 9
                size_t cmp_cap = to_comp_len + to_comp_len/10 + 65536;
                uint8_t* cmp = (uint8_t*)malloc(cmp_cap);
                if (!cmp) { if (filtered) free(filtered); free(raw); free(objs); return 0; }
                size_t cmp_len = deflate_compress(to_comp, to_comp_len, cmp, cmp_cap, 9);
                if (cmp_len == 0) {
                    // Fallback: keep original
                    rec->new_stream = (uint8_t*)malloc(stream_len);
                    if (!rec->new_stream) { if (filtered) free(filtered); free(cmp); free(raw); free(objs); return 0; }
                    memcpy(rec->new_stream, stream_start, stream_len);
                    rec->new_stream_len = stream_len;
                } else {
                    rec->new_stream = cmp; rec->new_stream_len = cmp_len;
                    // filtered buffer re-used only as source, free if allocated
                    cmp = NULL;
                }
                if (filtered) free(filtered);
                free(raw);
            }
        }
#else
        // Without miniz, pass through original stream unmodified
        if (has_flate && stream_start && stream_len > 0) {
            rec->new_stream = (uint8_t*)malloc(stream_len);
            if (!rec->new_stream) { free(objs); return 0; }
            memcpy(rec->new_stream, stream_start, stream_len);
            rec->new_stream_len = stream_len;
        }
#endif
        i = obj_end + 6; // skip past "endobj"
    }

    // Sort objects with streams by new_stream_len descending to group similar sizes
    for (int a = 0; a < obj_cnt; a++) {
        for (int bidx = a + 1; bidx < obj_cnt; bidx++) {
            size_t la = objs[a].new_stream_len;
            size_t lb = objs[bidx].new_stream_len;
            if (lb > la) {
                ObjRec tmp = objs[a]; objs[a] = objs[bidx]; objs[bidx] = tmp;
            }
        }
    }

    // Begin writing reconstructed PDF
    size_t off = 0;
    if (!buf_append(out, out_cap, &off, pdf, 8)) { // copy header line (first 8 bytes enough: %PDF-1.x)
        for (int k = 0; k < obj_cnt; k++) { if (objs[k].new_stream) free(objs[k].new_stream); }
        free(objs); return 0;
    }
    if (!buf_append(out, out_cap, &off, "\n", 1)) {
        for (int k = 0; k < obj_cnt; k++) { if (objs[k].new_stream) free(objs[k].new_stream); }
        free(objs); return 0;
    }

    size_t* obj_offsets = (size_t*)malloc(sizeof(size_t) * (size_t)obj_cnt);
    if (!obj_offsets) {
        for (int k = 0; k < obj_cnt; k++) { if (objs[k].new_stream) free(objs[k].new_stream); }
        free(objs); return 0;
    }

    char linebuf[128];
    for (int idx = 0; idx < obj_cnt; idx++) {
        ObjRec* r = &objs[idx];
        obj_offsets[idx] = off;
        int llen = snprintf(linebuf, sizeof(linebuf), "%d %d obj\n", r->num, r->gen);
        if (!buf_append(out, out_cap, &off, linebuf, (size_t)llen)) { off = 0; break; }

        // Rebuild dictionary: ensure /Length matches new stream length, add DecodeParms if filtered was applied (heuristic omitted here)
        // Replace any /Length ... with numeric length
        const uint8_t* dict = r->dict_start; size_t dlen = r->dict_len;
        const uint8_t* lpos = dict ? find_sub(dict, dlen, "/Length") : NULL;
        if (lpos && r->new_stream && r->new_stream_len > 0) {
            // Write dict until /Length
            size_t pre = (size_t)(lpos - dict);
            if (!buf_append(out, out_cap, &off, dict, pre)) { off = 0; break; }
            int slen = snprintf(linebuf, sizeof(linebuf), "/Length %zu ", r->new_stream_len);
            if (!buf_append(out, out_cap, &off, linebuf, (size_t)slen)) { off = 0; break; }
            // Write remainder after old length token up to end of dict
            // Skip old token until next '/' or '>>'
            size_t lidx = (size_t)(lpos - dict);
            size_t lend = lidx;
            while (lend < dlen && dict[lend] != '/' && dict[lend] != '>') lend++;
            if (!buf_append(out, out_cap, &off, dict + lend, dlen - lend)) { off = 0; break; }
        } else {
            if (!buf_append(out, out_cap, &off, dict, dlen)) { off = 0; break; }
        }

        if (!buf_append(out, out_cap, &off, "\n", 1)) { off = 0; break; }
        if (r->new_stream && r->new_stream_len > 0) {
            if (!buf_append(out, out_cap, &off, "stream\n", 7)) { off = 0; break; }
            if (!buf_append(out, out_cap, &off, r->new_stream, r->new_stream_len)) { off = 0; break; }
            if (!buf_append(out, out_cap, &off, "\nendstream\nendobj\n", 18)) { off = 0; break; }
        } else {
            // Non-stream or unchanged stream
            if (r->stream_start && r->stream_len > 0) {
                if (!buf_append(out, out_cap, &off, "stream\n", 7)) { off = 0; break; }
                if (!buf_append(out, out_cap, &off, r->stream_start, r->stream_len)) { off = 0; break; }
                if (!buf_append(out, out_cap, &off, "\nendstream\nendobj\n", 18)) { off = 0; break; }
            } else {
                if (!buf_append(out, out_cap, &off, "endobj\n", 8)) { off = 0; break; }
            }
        }
    }

    // Write xref and trailer
    if (off != 0) {
        if (trailer && trailer_len > 0) {
            if (!write_xref_and_trailer(out, out_cap, &off, obj_offsets, obj_cnt, trailer, trailer_len)) {
                off = 0;
            }
        } else {
            // Minimal trailer if none found
            const char* tr_min = "trailer\n<< /Size %d >>\nstartxref\n";
            int sl = snprintf(linebuf, sizeof(linebuf), tr_min, obj_cnt + 1);
            if (!buf_append(out, out_cap, &off, linebuf, (size_t)sl)) off = 0;
            unsigned long long sx = (unsigned long long)off;
            int sxlen = snprintf(linebuf, sizeof(linebuf), "%llu\n%%EOF\n", sx);
            if (!buf_append(out, out_cap, &off, linebuf, (size_t)sxlen)) off = 0;
        }
    }

    // Cleanup
    for (int k = 0; k < obj_cnt; k++) { if (objs[k].new_stream) free(objs[k].new_stream); }
    free(objs);
    free(obj_offsets);
    return off;
}

// Decompressor stub (identity copy for now; full reversal requires original mapping)
size_t pdf_decompress(const uint8_t* cmp, size_t cmp_len, uint8_t* out, size_t out_cap) {
    if (!cmp || !out || out_cap < cmp_len) return 0;
    memcpy(out, cmp, cmp_len);
    return cmp_len;
}