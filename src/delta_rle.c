#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ONE_MB (1024u * 1024u)
#define RUN_MARKER 0xFFu

static inline uint8_t delta_byte(uint8_t cur, uint8_t prev) {
    return (uint8_t)(cur - prev);
}

// Emit a single literal byte, escaping the run marker as [RUN_MARKER][0][value]
static int emit_literal(uint8_t v, uint8_t* out, size_t out_cap, size_t* produced) {
    if (v == RUN_MARKER) {
        if (*produced + 3 > out_cap) return 0;
        out[(*produced)++] = RUN_MARKER;
        out[(*produced)++] = 0;            // escape indicator for literal
        out[(*produced)++] = RUN_MARKER;   // actual literal value (0xFF)
        return 1;
    }
    if (*produced >= out_cap) return 0;
    out[(*produced)++] = v;
    return 1;
}

// Flush a run of `value` repeated `*run` times.
// Runs >= 4 are emitted as RLE: [RUN_MARKER][count][value], splitting if count > 255.
// Shorter runs are emitted as literals.
static int flush_run(uint8_t value, size_t* run, uint8_t* out, size_t out_cap, size_t* produced) {
    size_t count = *run;
    if (count >= 4) {
        while (count > 0) {
            uint8_t chunk = (uint8_t)(count > 255 ? 255 : count);
            if (*produced + 3 > out_cap) return 0;
            out[(*produced)++] = RUN_MARKER;
            out[(*produced)++] = chunk;
            out[(*produced)++] = value;
            count -= chunk;
        }
    } else {
        for (size_t i = 0; i < count; i++) {
            if (!emit_literal(value, out, out_cap, produced)) return 0;
        }
    }
    return 1;
}

// Preprocess input with delta (first 1MB) + RLE (runs >= 4).
// Note: This is a transform intended to improve compressibility before LZMA/DEFLATE.
// The caller should only adopt this buffer if it reduces size, otherwise fall back.
size_t delta_rle_pre(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    if (!in || !out || out_cap == 0) return 0;
    if (in_len == 0) return 0;

    const size_t delta_limit = (in_len < ONE_MB) ? in_len : ONE_MB;

    // Streaming RLE over delta-transformed prefix and literal tail
    size_t produced = 0;

    // Initialize with first byte (delta: original for position 0)
    uint8_t prev_orig = in[0];
    uint8_t prev = prev_orig; // first emitted value
    size_t run = 1;

    for (size_t i = 1; i < in_len; i++) {
        uint8_t cur = in[i];
        uint8_t val;
        if (i < delta_limit) {
            val = delta_byte(cur, prev_orig);
            prev_orig = cur;
        } else {
            val = cur;
        }

        if (val == prev && run < SIZE_MAX) {
            // Avoid overflow: cap per-chunk at 255 during flush
            run++;
        } else {
            if (!flush_run(prev, &run, out, out_cap, &produced)) return 0;
            prev = val;
            run = 1;
        }
    }

    // Flush final run
    if (!flush_run(prev, &run, out, out_cap, &produced)) return 0;

    // Fallback to original if expansion exceeds 2%
    size_t expansion_threshold = (size_t)((double)in_len * 1.02);
    if (produced > expansion_threshold) {
        if (out_cap < in_len) return 0;
        memcpy(out, in, in_len);
        return in_len;
    }

    return produced;
}

// Inverse transform: decode RLE stream and reverse delta on first 1MB.
// Decoding rules mirror encoder:
// - Literals: any byte except RUN_MARKER emits directly.
// - Escaped literal 0xFF: [RUN_MARKER][0][RUN_MARKER] → emit 0xFF.
// - Runs: [RUN_MARKER][count][value] → emit 'value' 'count' times (count ∈ [1..255]).
// After RLE decode, apply delta inversion over the first min(out_len, ONE_MB) bytes:
// - Position 0 is original literal.
// - For positions 1..delta_limit-1: orig[i] = delta[i] + orig[i-1].
size_t delta_rle_post(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap) {
    if (!in || !out || out_cap == 0) return 0;
    if (in_len == 0) return 0;

    size_t produced = 0;
    size_t i = 0;

    while (i < in_len) {
        uint8_t b = in[i++];
        if (b == RUN_MARKER) {
            if (i >= in_len) return 0; // truncated stream
            uint8_t count = in[i++];
            if (count == 0) {
                // Escaped literal 0xFF
                if (i >= in_len) return 0;
                uint8_t lit = in[i++];
                if (produced >= out_cap) return 0;
                out[produced++] = lit; // expected to be 0xFF
            } else {
                // Run of 'count' bytes of 'value'
                if (i >= in_len) return 0;
                uint8_t value = in[i++];
                if (produced + count > out_cap) return 0;
                for (uint8_t r = 0; r < count; r++) {
                    out[produced++] = value;
                }
            }
        } else {
            // Literal byte
            if (produced >= out_cap) return 0;
            out[produced++] = b;
        }
    }

    if (produced == 0) return 0;

    // Reverse delta on the first 1MB region
    const size_t delta_limit = (produced < ONE_MB) ? produced : ONE_MB;
    uint8_t prev = out[0];
    for (size_t pos = 1; pos < delta_limit; pos++) {
        uint8_t d = out[pos];
        uint8_t orig = (uint8_t)(d + prev);
        out[pos] = orig;
        prev = orig;
    }

    return produced;
}