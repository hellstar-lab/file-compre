// Realtime clone-and-compress server
// - AF_UNIX socket at /tmp/comp.sock
// - Parent limits concurrency to `nproc --all` (fallback to sysconf)
// - Child reads: [8-byte big-endian path_len] + [path bytes]
// - Compresses using intelligent_compress_file() with O_DIRECT I/O
// - Writes COMP v2 header + payload using O_DIRECT, padding to alignment then ftruncate
// - Responds: [1-byte CompResult] + [8-byte big-endian compressed_size]

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "../include/compressor.h"

#ifndef O_DIRECT
#define O_DIRECT 0
#endif

#define SOCK_PATH "/tmp/comp.sock"
#define MAX_PATH_BYTES 4096
#define METRICS_PORT 9100

static volatile sig_atomic_t g_active_children = 0;

// Prometheus metrics (atomic)
static volatile unsigned long long g_bytes_saved_total = 0ULL;
static volatile double g_lz77_latency_seconds_last = 0.0;
static volatile double g_lz77_latency_seconds_sum = 0.0;
static volatile unsigned long long g_lz77_latency_count = 0ULL;

static void sigchld_handler(int signo) {
    (void)signo;
    int status;
    // Reap all finished children
    while (waitpid(-1, &status, WNOHANG) > 0) {
        if (g_active_children > 0) g_active_children--;
    }
}

static int read_full(int fd, void* buf, size_t len) {
    unsigned char* p = (unsigned char*)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t r = read(fd, p + off, len - off);
        if (r == 0) return -1; // EOF
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)r;
    }
    return 0;
}

static int write_full(int fd, const void* buf, size_t len) {
    const unsigned char* p = (const unsigned char*)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static uint64_t be64_decode(const unsigned char b[8]) {
    return ((uint64_t)b[0] << 56) |
           ((uint64_t)b[1] << 48) |
           ((uint64_t)b[2] << 40) |
           ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) |
           ((uint64_t)b[5] << 16) |
           ((uint64_t)b[6] << 8)  |
            (uint64_t)b[7];
}

static void be64_encode(uint64_t v, unsigned char b[8]) {
    b[0] = (unsigned char)((v >> 56) & 0xFF);
    b[1] = (unsigned char)((v >> 48) & 0xFF);
    b[2] = (unsigned char)((v >> 40) & 0xFF);
    b[3] = (unsigned char)((v >> 32) & 0xFF);
    b[4] = (unsigned char)((v >> 24) & 0xFF);
    b[5] = (unsigned char)((v >> 16) & 0xFF);
    b[6] = (unsigned char)((v >> 8) & 0xFF);
    b[7] = (unsigned char)(v & 0xFF);
}

static size_t direct_alignment(void) {
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    return (size_t)ps;
}

static int read_file_direct_or_fallback(const char* path, unsigned char** out_buf, long* out_size) {
    *out_buf = NULL; *out_size = 0;
    int fd = open(path, O_RDONLY | O_DIRECT);
    struct stat st;
    if (fd < 0) {
        // Fallback: normal read
        unsigned char* buf = NULL; long sz = 0;
        if (read_file(path, &buf, &sz) != 0) return -1;
        *out_buf = buf; *out_size = sz;
        return 0;
    }
    if (fstat(fd, &st) != 0) { close(fd); return -1; }
    long size = (long)st.st_size;
    size_t align = direct_alignment();
    size_t padded = ((size + align - 1) / align) * align;
    unsigned char* buf = NULL;
    if (posix_memalign((void**)&buf, align, padded) != 0) { close(fd); return -1; }
    memset(buf, 0, padded);
    long read_total = 0;
    while (read_total < size) {
        ssize_t r = read(fd, buf + read_total, (size_t)(size - read_total));
        if (r < 0) {
            if (errno == EINTR) continue;
            // If direct read fails mid-way, fallback via normal read
            free(buf); close(fd);
            unsigned char* fbuf = NULL; long fsz = 0;
            if (read_file(path, &fbuf, &fsz) != 0) return -1;
            *out_buf = fbuf; *out_size = fsz;
            return 0;
        }
        if (r == 0) break;
        read_total += (long)r;
    }
    close(fd);
    *out_buf = buf;
    *out_size = size;
    return 0;
}

static int direct_write_all(int fd, const unsigned char* data, long size) {
    size_t align = direct_alignment();
    size_t padded = ((size + align - 1) / align) * align;
    unsigned char* wbuf = NULL;
    if (posix_memalign((void**)&wbuf, align, padded) != 0) return -1;
    memset(wbuf, 0, padded);
    memcpy(wbuf, data, (size_t)size);
    size_t off = 0;
    while (off < padded) {
        ssize_t w = write(fd, wbuf + off, padded - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            free(wbuf);
            return -1;
        }
        off += (size_t)w;
    }
    // shrink to actual size
    if (ftruncate(fd, (off_t)size) != 0) {
        free(wbuf);
        return -1;
    }
    free(wbuf);
    return 0;
}

// Minimal HTTP metrics server (Prometheus text format)
static void* metrics_thread_func(void* arg) {
    (void)arg;
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) return NULL;

    int opt = 1;
    (void)setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(METRICS_PORT);

    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sfd);
        return NULL;
    }
    if (listen(sfd, 16) < 0) {
        close(sfd);
        return NULL;
    }

    for (;;) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Very small request buffer; we don't parse fully
        char req[256];
        (void)read(cfd, req, sizeof(req));

        // Snapshot metrics atomically
        unsigned long long bytes_saved = __atomic_load_n(&g_bytes_saved_total, __ATOMIC_RELAXED);
        double lz77_last = __atomic_load_n(&g_lz77_latency_seconds_last, __ATOMIC_RELAXED);

        char body[512];
        int blen = snprintf(body, sizeof(body),
            "# HELP comp_bytes_saved_total Description of total bytes saved\n"
            "# TYPE comp_bytes_saved_total counter\n"
            "comp_bytes_saved_total %.6e\n"
            "comp_latency_seconds{algo=\"LZ77\"} %.6f\n",
            (double)bytes_saved, lz77_last);
        if (blen < 0) blen = 0;
        if (blen > (int)sizeof(body)) blen = (int)sizeof(body);

        char hdr[256];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n", blen);
        if (hlen > 0) (void)write_full(cfd, hdr, (size_t)hlen);
        if (blen > 0) (void)write_full(cfd, body, (size_t)blen);
        close(cfd);
    }

    close(sfd);
    return NULL;
}

// Clone-and-compress with algorithm selection and O_DIRECT I/O
static CompResult intelligent_compress_file(const char* input_path, char* out_path, size_t out_path_cap, uint64_t* out_comp_size) {
    CompResult ret = COMP_OK;
    unsigned char* input = NULL;
    long input_size = 0;
    unsigned char* output = NULL;
    long output_size = 0;
    time_t start_ms = 0, end_ms = 0;

    if (!input_path || !out_path || !out_comp_size) return COMP_ERROR_INVALID_PARAM;

    // Read input using O_DIRECT if possible
    if (read_file_direct_or_fallback(input_path, &input, &input_size) != 0 || !input || input_size <= 0) {
        ret = COMP_ERROR_FILE_READ;
        goto fail;
    }

    // Compute metrics and decide algorithm
    double entropy = 0.0, ascii_ratio = 0.0, repeat_freq = 0.0; int is_binary = 0;
    Compressor_Test_ComputeMetrics(input, (size_t)input_size, &entropy, &ascii_ratio, &repeat_freq, &is_binary);
    CompressionAlgorithm algo = Compressor_Test_Select(entropy, ascii_ratio, repeat_freq, is_binary);

    // Early termination check (5%)
    double chunk_ratio = 100.0; long chunk_size_out = 0;
    (void)Compressor_Test_CheckEarly(algo, input, input_size, &chunk_ratio, &chunk_size_out);

    // Compress buffer using selected algorithm (measure latency)
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = -1;
    switch (algo) {
        case ALGO_HUFFMAN: rc = huffman_compress(input, input_size, &output, &output_size); break;
        case ALGO_LZ77:    rc = lz77_compress(input, input_size, &output, &output_size);    break;
        case ALGO_LZW:     rc = lzw_compress(input, input_size, &output, &output_size);     break;
        case ALGO_HARDCORE:rc = hardcore_compress(input, input_size, &output, &output_size);break;
        default:           rc = -1; break;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double latency = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    if (algo == ALGO_LZ77) {
        __atomic_store_n(&g_lz77_latency_seconds_last, latency, __ATOMIC_RELAXED);
        (void)__atomic_fetch_add(&g_lz77_latency_seconds_sum, latency, __ATOMIC_RELAXED);
        (void)__atomic_fetch_add(&g_lz77_latency_count, 1ULL, __ATOMIC_RELAXED);
    }
    if (rc != 0 || !output || output_size <= 0) {
        ret = COMP_ERROR_COMPRESSION_FAILED;
        goto fail;
    }

    // Compose output path: alongside input, append .comp
    const char* slash = strrchr(input_path, '/');
    const char* base = slash ? (slash + 1) : input_path;
    char dir[1024];
    if (slash) {
        size_t dlen = (size_t)(slash - input_path);
        if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
        memcpy(dir, input_path, dlen);
        dir[dlen] = '\0';
        snprintf(out_path, out_path_cap, "%.*s/%s.comp", (int)dlen, input_path, base);
    } else {
        snprintf(out_path, out_path_cap, "%s.comp", base);
    }

    // Build v2 COMP header (64 bytes)
    unsigned char header[64];
    memset(header, 0, sizeof(header));
    header[0] = 'C'; header[1] = 'O'; header[2] = 'M'; header[3] = 'P';
    header[4] = 2; // version
    header[5] = (unsigned char)algo;
    header[6] = (unsigned char)COMPRESSION_LEVEL_NORMAL;
    header[7] = 0;
    for (int i = 0; i < 8; i++) header[8 + i]  = (unsigned char)((((uint64_t)input_size)   >> ((7 - i) * 8)) & 0xFF);
    for (int i = 0; i < 8; i++) header[16 + i] = (unsigned char)((((uint64_t)output_size)  >> ((7 - i) * 8)) & 0xFF);
    uint32_t comp_time_ms = 0; // simplified
    for (int i = 0; i < 4; i++) header[24 + i] = (unsigned char)((comp_time_ms >> ((3 - i) * 8)) & 0xFF);
    uint32_t mem_kb = 0; // simplified, could pull from memory pool
    for (int i = 0; i < 4; i++) header[28 + i] = (unsigned char)((mem_kb >> ((3 - i) * 8)) & 0xFF);

    // Prepare single contiguous buffer: header + payload
    long combined_size = (long)sizeof(header) + output_size;
    unsigned char* combined = (unsigned char*)malloc((size_t)combined_size);
    if (!combined) { ret = COMP_ERROR_MEMORY; goto fail; }
    memcpy(combined, header, sizeof(header));
    memcpy(combined + sizeof(header), output, (size_t)output_size);

    // Open output with O_DIRECT (fallback to normal write if needed)
    int ofd = open(out_path, O_CREAT | O_TRUNC | O_WRONLY | O_DIRECT, 0644);
    if (ofd < 0) {
        // Fallback to normal write if O_DIRECT unsupported
        ofd = open(out_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    }
    if (ofd < 0) { free(combined); ret = COMP_ERROR_FILE_WRITE; goto fail; }

    if (direct_write_all(ofd, combined, combined_size) != 0) {
        // Fallback: rewrite without O_DIRECT
        close(ofd);
        ofd = open(out_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (ofd < 0) { free(combined); ret = COMP_ERROR_FILE_WRITE; goto fail; }
        if (write_full(ofd, combined, (size_t)combined_size) != 0) { close(ofd); free(combined); ret = COMP_ERROR_FILE_WRITE; goto fail; }
        close(ofd);
    } else {
        close(ofd);
    }

    *out_comp_size = (uint64_t)combined_size;
    free(combined);

    // Update bytes saved counter atomically
    long saved = input_size - output_size;
    if (saved > 0) {
        (void)__atomic_fetch_add(&g_bytes_saved_total, (unsigned long long)saved, __ATOMIC_RELAXED);
    }
    ret = COMP_OK;

fail:
    if (input) COMP_FREE(input);
    if (output) COMP_FREE(output);
    return ret;
}

static int get_max_procs(void) {
    // Prefer external `nproc --all`
    FILE* f = popen("nproc --all", "r");
    if (f) {
        char buf[64] = {0};
        if (fgets(buf, sizeof(buf), f)) {
            int n = atoi(buf);
            pclose(f);
            if (n > 0) return n;
        }
        pclose(f);
    }
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 0) n = 1;
    return (int)n;
}

int main(void) {
    // Install SIGCHLD handler for concurrency accounting
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    // Prepare socket file
    unlink(SOCK_PATH);
    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr; memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sfd);
        return 1;
    }
    if (listen(sfd, 128) < 0) { perror("listen"); close(sfd); return 1; }

    int max_procs = get_max_procs();
    fprintf(stderr, "[realtime] listening on %s (max children=%d)\n", SOCK_PATH, max_procs);

    for (;;) {
        // Back-pressure: wait until capacity is available before accept
        while (g_active_children >= max_procs) {
            // Block until a child exits
            int status;
            if (waitpid(-1, &status, 0) > 0) {
                if (g_active_children > 0) g_active_children--;
            }
        }

        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(cfd);
            continue;
        }
        if (pid == 0) {
            // Child process: handle request
            unsigned char len_buf[8];
            if (read_full(cfd, len_buf, sizeof(len_buf)) != 0) { close(cfd); _exit(0); }
            uint64_t path_len = be64_decode(len_buf);
            if (path_len == 0 || path_len > MAX_PATH_BYTES) { close(cfd); _exit(0); }
            char* path = (char*)malloc((size_t)path_len + 1);
            if (!path) { close(cfd); _exit(0); }
            if (read_full(cfd, path, (size_t)path_len) != 0) { free(path); close(cfd); _exit(0); }
            path[path_len] = '\0';

            char out_path[1024];
            uint64_t comp_size = 0;
            CompResult status = intelligent_compress_file(path, out_path, sizeof(out_path), &comp_size);

            unsigned char status_byte = (unsigned char)status;
            unsigned char sz_buf[8]; be64_encode(comp_size, sz_buf);
            // Respond to client
            (void)write_full(cfd, &status_byte, 1);
            (void)write_full(cfd, sz_buf, sizeof(sz_buf));
            close(cfd);
            _exit(0);
        } else {
            // Parent: record child and close client fd
            g_active_children++;
            close(cfd);
        }
    }

    // Not reached
    close(sfd);
    return 0;
}
    // Start metrics HTTP server thread (detached)
    pthread_t mtid;
    if (pthread_create(&mtid, NULL, metrics_thread_func, NULL) == 0) {
        pthread_detach(mtid);
    }