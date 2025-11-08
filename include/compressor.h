#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include "comp_result.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <setjmp.h>

// Maximum file path length
#define MAX_PATH_LENGTH 260
#define MAX_FILENAME_LENGTH 100
#define BUFFER_SIZE 8192

// File type enumeration
typedef enum {
    FILE_TYPE_TEXT,
    FILE_TYPE_PDF,
    FILE_TYPE_DOCX,
    FILE_TYPE_XML,
    FILE_TYPE_AUDIO,
    FILE_TYPE_CSV,
    FILE_TYPE_JSON,
    // Specific image formats (24-bit focus)
    FILE_TYPE_BMP,
    FILE_TYPE_PNG,
    FILE_TYPE_TGA,
    FILE_TYPE_IMAGE,
    FILE_TYPE_UNKNOWN
} FileType;

// Compression algorithm enumeration
typedef enum {
    ALGO_HUFFMAN,
    ALGO_LZ77,
    ALGO_LZW,
    ALGO_AUDIO_ADVANCED,
    ALGO_IMAGE_ADVANCED,
    ALGO_HARDCORE,  // 5-stage pipeline: BWT+MTF+RLE+Dict+Huffman
    ALGO_BLOCKWISE,  // Container with per-block algorithm selection
    ALGO_DEFLATE,    // Drop-in DEFLATE (miniz/zlib)
    ALGO_LZMA        // LZMA (7-Zip SDK wrapper)
} CompressionAlgorithm;

// Compression level enumeration
typedef enum {
    COMPRESSION_LEVEL_FAST = 1,
    COMPRESSION_LEVEL_NORMAL = 2,
    COMPRESSION_LEVEL_HIGH = 3,
    COMPRESSION_LEVEL_ULTRA = 4
} CompressionLevel;

// Huffman tree node structure
typedef struct HuffmanNode {
    unsigned char data;
    unsigned int frequency;
    struct HuffmanNode* left;
    struct HuffmanNode* right;
} HuffmanNode;

// Compression statistics structure
typedef struct {
    char original_filename[MAX_FILENAME_LENGTH];
    char compressed_filename[MAX_FILENAME_LENGTH];
    long original_size;
    long compressed_size;
    double compression_ratio;
    CompressionAlgorithm algorithm_used;
    CompressionLevel compression_level;
    time_t compression_time;
    double compression_speed; // MB/s
    double memory_usage; // MB
} CompressionStats;

// File information structure
typedef struct {
    char filepath[MAX_PATH_LENGTH];
    char filename[MAX_FILENAME_LENGTH];
    FileType type;
    long size;
} FileInfo;

// Function prototypes

// File operations
FileType detect_file_type(const char* filename);
FileType detect_file_type_enhanced(const char* filename, const unsigned char* data, size_t size);
int read_file(const char* filepath, unsigned char** buffer, long* size);
int write_file(const char* filepath, const unsigned char* buffer, long size);
int copy_file_to_data_folder(const char* source_path, char* dest_path);

// Huffman compression
HuffmanNode* create_huffman_node(unsigned char data, unsigned int frequency);
HuffmanNode* build_huffman_tree(unsigned int* frequencies);
void generate_huffman_codes(HuffmanNode* root, char codes[256][256], char* current_code, int depth);
CompResult huffman_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);
CompResult huffman_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);
void free_huffman_tree(HuffmanNode* root);

// LZ77 compression
int lz77_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);
int lz77_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);

// LZW compression
int lzw_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);
int lzw_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);

// Main compression/decompression functions
CompResult compress_file(const char* input_path, const char* output_path, CompressionAlgorithm algo, CompressionStats* stats);
CompResult compress_file_with_level(const char* input_path, const char* output_path, CompressionAlgorithm algo, CompressionLevel level, CompressionStats* stats);
CompResult compress_file_intelligent(const char* input_path, const char* output_path, CompressionLevel level, CompressionStats* stats);
CompResult decompress_file(const char* input_path, const char* output_path, CompressionStats* stats);

// Blockwise intelligent compression (v4 container)
CompResult compress_file_blockwise(const unsigned char* input_buffer, long input_size,
                            const char* output_path, FileType file_type,
                            CompressionLevel level, CompressionStats* stats);

// Advanced audio compression functions
int audio_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size, int level);
int audio_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);

// Advanced image compression functions
int image_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size, int level);
int image_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);

// Utility functions
void print_compression_stats(const CompressionStats* stats);
void print_menu();
int get_user_choice();
char* get_file_extension(const char* filename);
void generate_compressed_filename(const char* original_filename, char* compressed_filename);
void generate_decompressed_filename(const char* compressed_filename, char* decompressed_filename);
void generate_decompressed_filename_with_ext(const char* compressed_filename, const char* original_extension, char* decompressed_filename);

// Selector test utilities (for unit testing)
CompressionAlgorithm Compressor_Test_Select(double entropy, double ascii_ratio, double repeat_freq, int is_binary);
void Compressor_Test_ComputeMetrics(const unsigned char* data, size_t size, double* entropy, double* ascii_ratio, double* repeat_freq, int* is_binary);
int Compressor_Test_CheckEarly(CompressionAlgorithm algo, const unsigned char* input, long input_size, double* out_ratio_percent, long* out_partial_size);

// File management
int list_files_in_directory(const char* directory_path);
int delete_file(const char* filepath);

// Hardcore multi-stage compression (40-60% compression ratio)
int hardcore_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);
int hardcore_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);

// BWT+MTF+Huffman optimized compression
int bwt_mtf_huffman_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);
int bwt_mtf_huffman_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size);

// Memory tracking functions
void* tracked_malloc(size_t size);
void tracked_free(void* ptr, size_t size);

// Memory pool management functions
int comp_init_memory_pool(void);
void comp_cleanup_memory_pool(void);
void* comp_malloc(size_t size);
void comp_free(void* ptr);
void comp_check_leaks(void);
void comp_panic(const char* message);

// Memory pool macros
#define COMP_MALLOC(size) comp_malloc(size)
#define COMP_FREE(ptr) comp_free(ptr)

// Global panic buffer for error recovery
extern jmp_buf g_panic_buf;

// Optimized decompression
CompResult huffman_decompress_optimized(const unsigned char* input, long input_size, unsigned char** output, long* output_size);

#endif // COMPRESSOR_H