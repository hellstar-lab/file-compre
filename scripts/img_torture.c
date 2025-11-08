// Image torture generator: creates 3 x 24-bit BMPs (800x600)
// Files:
//  - grad_800x600.bmp   : vertical grayscale gradient
//  - noise_800x600.bmp  : random pixel noise (seeded, reproducible)
//  - photo_800x600.bmp  : synthetic photo-like pattern (Lena placeholder)
// Each is exactly 1,440,054 bytes: 54-byte BMP header + 800*600*3 pixel bytes.
// Build (MSVC): cl scripts/img_torture.c /Fe:img_torture.exe

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static int write_bmp_24(const char* path, int w, int h, const uint8_t* topdown_pixels) {
    if (!path || !topdown_pixels) return -1;
    const int stride = w * 3; const uint32_t img_size = (uint32_t)(stride * h);
    const uint32_t file_size = 54u + img_size;

    FILE* f = fopen(path, "wb"); if (!f) return -1;
    uint8_t header[54]; memset(header, 0, sizeof(header));
    header[0] = 'B'; header[1] = 'M';
    header[2] = (uint8_t)(file_size & 0xFF);
    header[3] = (uint8_t)((file_size >> 8) & 0xFF);
    header[4] = (uint8_t)((file_size >> 16) & 0xFF);
    header[5] = (uint8_t)((file_size >> 24) & 0xFF);
    header[10] = 54;            // Pixel data offset
    header[14] = 40;            // DIB header size (BITMAPINFOHEADER)
    // Width
    header[18] = (uint8_t)(w & 0xFF); header[19] = (uint8_t)((w >> 8) & 0xFF);
    header[20] = (uint8_t)((w >> 16) & 0xFF); header[21] = (uint8_t)((w >> 24) & 0xFF);
    // Height (positive => bottom-up)
    header[22] = (uint8_t)(h & 0xFF); header[23] = (uint8_t)((h >> 8) & 0xFF);
    header[24] = (uint8_t)((h >> 16) & 0xFF); header[25] = (uint8_t)((h >> 24) & 0xFF);
    header[26] = 1;             // planes
    header[28] = 24;            // bpp
    // Image size
    header[34] = (uint8_t)(img_size & 0xFF); header[35] = (uint8_t)((img_size >> 8) & 0xFF);
    header[36] = (uint8_t)((img_size >> 16) & 0xFF); header[37] = (uint8_t)((img_size >> 24) & 0xFF);
    // PPM (72 DPI ~ 2835 ppm)
    const uint32_t ppm = 2835;
    header[38] = (uint8_t)(ppm & 0xFF); header[39] = (uint8_t)((ppm >> 8) & 0xFF);
    header[40] = (uint8_t)((ppm >> 16) & 0xFF); header[41] = (uint8_t)((ppm >> 24) & 0xFF);
    header[42] = (uint8_t)(ppm & 0xFF); header[43] = (uint8_t)((ppm >> 8) & 0xFF);
    header[44] = (uint8_t)((ppm >> 16) & 0xFF); header[45] = (uint8_t)((ppm >> 24) & 0xFF);

    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) { fclose(f); return -1; }
    // Write bottom-up rows
    for (int y = h - 1; y >= 0; y--) {
        const uint8_t* row = topdown_pixels + (size_t)y * (size_t)stride;
        if (fwrite(row, 1, (size_t)stride, f) != (size_t)stride) { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

static uint8_t* make_gradient(int w, int h) {
    const int stride = w * 3; size_t total = (size_t)stride * (size_t)h;
    uint8_t* buf = (uint8_t*)malloc(total); if (!buf) return NULL;
    for (int y = 0; y < h; y++) {
        uint8_t g = (uint8_t)((y * 255) / (h - 1));
        uint8_t* row = buf + (size_t)y * (size_t)stride;
        for (int x = 0; x < w; x++) {
            row[x*3 + 0] = g; // B
            row[x*3 + 1] = g; // G
            row[x*3 + 2] = g; // R
        }
    }
    return buf;
}

static uint32_t xorshift32(uint32_t* s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x; return x;
}

static uint8_t* make_noise(int w, int h) {
    const int stride = w * 3; size_t total = (size_t)stride * (size_t)h;
    uint8_t* buf = (uint8_t*)malloc(total); if (!buf) return NULL;
    uint32_t seed = 1337u;
    for (int y = 0; y < h; y++) {
        uint8_t* row = buf + (size_t)y * (size_t)stride;
        for (int x = 0; x < w; x++) {
            uint32_t r = xorshift32(&seed);
            row[x*3 + 0] = (uint8_t)(r & 0xFF);
            row[x*3 + 1] = (uint8_t)((r >> 8) & 0xFF);
            row[x*3 + 2] = (uint8_t)((r >> 16) & 0xFF);
        }
    }
    return buf;
}

// Synthetic photo-like: radial vignette + cosine waves + soft edges.
static uint8_t* make_photo_like(int w, int h) {
    const int stride = w * 3; size_t total = (size_t)stride * (size_t)h;
    uint8_t* buf = (uint8_t*)malloc(total); if (!buf) return NULL;
    const double cx = w * 0.5, cy = h * 0.5;
    for (int y = 0; y < h; y++) {
        uint8_t* row = buf + (size_t)y * (size_t)stride;
        for (int x = 0; x < w; x++) {
            double dx = (x - cx) / (double)w;
            double dy = (y - cy) / (double)h;
            double r = dx*dx + dy*dy;
            double vignette = 1.0 - (r * 1.2);
            if (vignette < 0.0) vignette = 0.0;
            double wx = 0.5 + 0.5 * cos(2.0 * 3.14159265 * x / 64.0);
            double wy = 0.5 + 0.5 * cos(2.0 * 3.14159265 * y / 48.0);
            double edge = 0.5 + 0.5 * cos(2.0 * 3.14159265 * (dx*cx + dy*cy) / 96.0);
            double lum = vignette * (0.6 * wx + 0.4 * wy) * edge;
            double rch = lum * 220.0 + 35.0;
            double gch = lum * 210.0 + 45.0;
            double bch = lum * 200.0 + 55.0;
            if (rch > 255.0) rch = 255.0; if (gch > 255.0) gch = 255.0; if (bch > 255.0) bch = 255.0;
            if (rch < 0.0) rch = 0.0;    if (gch < 0.0) gch = 0.0;    if (bch < 0.0) bch = 0.0;
            row[x*3 + 0] = (uint8_t)bch;
            row[x*3 + 1] = (uint8_t)gch;
            row[x*3 + 2] = (uint8_t)rch;
        }
    }
    return buf;
}

int main(void) {
    const int W = 800, H = 600; const int stride = W*3; const size_t pixels = (size_t)stride*(size_t)H;

    uint8_t* grad = make_gradient(W,H);
    uint8_t* noise = make_noise(W,H);
    uint8_t* photo = make_photo_like(W,H);
    if (!grad || !noise || !photo) {
        fprintf(stderr, "Allocation failure\n");
        free(grad); free(noise); free(photo); return 1;
    }

    if (write_bmp_24("grad_800x600.bmp", W, H, grad) != 0) { fprintf(stderr, "Write grad failed\n"); }
    if (write_bmp_24("noise_800x600.bmp", W, H, noise) != 0) { fprintf(stderr, "Write noise failed\n"); }
    if (write_bmp_24("photo_800x600.bmp", W, H, photo) != 0) { fprintf(stderr, "Write photo failed\n"); }

    // Quick file size check
    const char* files[] = {"grad_800x600.bmp","noise_800x600.bmp","photo_800x600.bmp"};
    for (int i = 0; i < 3; i++) {
        FILE* f = fopen(files[i], "rb"); if (!f) continue;
        fseek(f, 0, SEEK_END); long sz = ftell(f); fclose(f);
        printf("%s size: %ld bytes\n", files[i], sz);
        if (sz != 1440054L) {
            fprintf(stderr, "Size mismatch for %s: expected 1440054, got %ld\n", files[i], sz);
        }
    }

    free(grad); free(noise); free(photo);
    return 0;
}