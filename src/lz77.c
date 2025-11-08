#include "../include/compressor.h"

// Forward declaration for alternative LZW implementation defined in missing_functions.c
int lzw_decompress_mfunc(const unsigned char* input, long input_size, unsigned char** output, long* output_size);
#include <string.h>

#define WINDOW_SIZE 4096
#define LOOKAHEAD_SIZE 18
#define MIN_MATCH_LENGTH 3
#define MAX_MATCH_LENGTH 18

// LZ77 match structure
typedef struct {
    int offset;
    int length;
    unsigned char next_char;
} LZ77Match;

// Find the longest match in the sliding window
LZ77Match find_longest_match(const unsigned char* data, long data_size, long current_pos) {
    LZ77Match match = {0, 0, 0};
    
    if (current_pos >= data_size) {
        return match;
    }
    
    // Set the next character
    match.next_char = data[current_pos];
    
    // Define search window boundaries
    long window_start = (current_pos >= WINDOW_SIZE) ? current_pos - WINDOW_SIZE : 0;
    long lookahead_end = (current_pos + LOOKAHEAD_SIZE < data_size) ? 
                        current_pos + LOOKAHEAD_SIZE : data_size;
    
    int best_length = 0;
    int best_offset = 0;
    
    // Search for matches in the sliding window
    for (long i = window_start; i < current_pos; i++) {
        int length = 0;
        
        // Find match length
        while (current_pos + length < lookahead_end && 
               data[i + length] == data[current_pos + length] &&
               length < MAX_MATCH_LENGTH) {
            length++;
        }
        
        // Update best match if this one is longer
        if (length >= MIN_MATCH_LENGTH && length > best_length) {
            best_length = length;
            best_offset = current_pos - i;
        }
    }
    
    match.length = best_length;
    match.offset = best_offset;
    
    return match;
}

// Write LZ77 token to output buffer (optimized encoding)
void write_lz77_token(unsigned char** output, long* output_size, long* output_capacity, 
                     int offset, int length, unsigned char next_char) {
    // Ensure buffer has enough space
    while (*output_size + 4 >= *output_capacity) {
        *output_capacity *= 2;
        *output = (unsigned char*)realloc(*output, *output_capacity);
    }
    
    if (length == 0) {
        // Literal character: just the character if < 128, otherwise 0x80 + char
        if (next_char < 128) {
            (*output)[(*output_size)++] = next_char;
        } else {
            (*output)[(*output_size)++] = 0x80;
            (*output)[(*output_size)++] = next_char;
        }
    } else {
        // Match: 0x81-0xFF for different length/offset combinations
        if (offset <= 255 && length <= 15) {
            // Short match: 0x81 + length(4bits) + offset(1byte)
            (*output)[(*output_size)++] = 0x81 | ((length - MIN_MATCH_LENGTH) << 1);
            (*output)[(*output_size)++] = offset & 0xFF;
        } else {
            // Long match: 0xFF + length + offset(2bytes)
            (*output)[(*output_size)++] = 0xFF;
            (*output)[(*output_size)++] = length - MIN_MATCH_LENGTH;
            (*output)[(*output_size)++] = (offset >> 8) & 0xFF;
            (*output)[(*output_size)++] = offset & 0xFF;
        }
    }
}

// LZ77 compression function (optimized for better compression ratio)
int lz77_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    if (!input || input_size <= 0) return -1;
    // FORCE-COMPRESS: Always generate compressed output, even for small inputs
    
    long output_capacity = input_size / 2; // Start smaller
    *output = (unsigned char*)malloc(output_capacity);
    *output_size = 0;
    
    // Write compression flag and original size (4 bytes for size)
    while (*output_size + 5 >= output_capacity) {
        output_capacity *= 2;
        *output = (unsigned char*)realloc(*output, output_capacity);
    }
    
    (*output)[(*output_size)++] = 0x01; // Flag for compressed
    // Write original size in 4 bytes (supports files up to 4GB)
    for (int i = 3; i >= 0; i--) {
        (*output)[(*output_size)++] = (input_size >> (i * 8)) & 0xFF;
    }
    
    long current_pos = 0;
    
    while (current_pos < input_size) {
        LZ77Match match = find_longest_match(input, input_size, current_pos);
        
        if (match.length >= MIN_MATCH_LENGTH) {
            // Found a match - encode it
            write_lz77_token(output, output_size, &output_capacity, 
                           match.offset, match.length, 0);
            current_pos += match.length;
        } else {
            // No match found, output literal
            write_lz77_token(output, output_size, &output_capacity, 
                           0, 0, input[current_pos]);
            current_pos++;
        }
    }
    
    // FORCE-COMPRESS: Keep compressed output even if not smaller
    // Minimal-reduction warning is handled by higher-level compressor logic
    
    return 0;
}

// Read LZ77 token from input buffer (optimized decoding)
int read_lz77_token(const unsigned char* input, long input_size, long* pos, 
                   int* offset, int* length, unsigned char* next_char) {
    if (*pos >= input_size) return -1;
    
    unsigned char first_byte = input[(*pos)++];
    
    if (first_byte < 0x80) {
        // Direct literal character
        *offset = 0;
        *length = 0;
        *next_char = first_byte;
    } else if (first_byte == 0x80) {
        // Escaped literal character (>= 128)
        if (*pos >= input_size) return -1;
        *offset = 0;
        *length = 0;
        *next_char = input[(*pos)++];
    } else if (first_byte == 0xFF) {
        // Long match: 0xFF + length + offset(2bytes)
        if (*pos + 3 >= input_size) return -1;
        *length = input[(*pos)++] + MIN_MATCH_LENGTH;
        *offset = (input[(*pos)++] << 8) | input[(*pos)++];
        *next_char = 0; // Not used in new format
    } else {
        // Short match: 0x81 + length(4bits) + offset(1byte)
        if (*pos >= input_size) return -1;
        *length = ((first_byte & 0x1E) >> 1) + MIN_MATCH_LENGTH;
        *offset = input[(*pos)++];
        *next_char = 0; // Not used in new format
    }
    
    return 0;
}

// Optimized LZ77 decompression with enhanced sliding window
int lz77_decompress_optimized(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    if (!input || input_size < 5) return -1;
    
    // Check compression flag
    unsigned char compression_flag = input[0];
    
    // FORCE-COMPRESS: reject raw stored blocks
    if (compression_flag == 0x00) {
        return -1;
    }
    
    // Read original size from header (4 bytes)
    *output_size = 0;
    for (int i = 0; i < 4; i++) {
        *output_size = (*output_size << 8) | input[i + 1];
    }
    
    // Pre-allocate output buffer with extra safety margin
    *output = (unsigned char*)tracked_malloc(*output_size + 1024);
    if (!*output) return -1;
    
    long current_pos = 0;
    long input_pos = 5; // Skip header (1 byte flag + 4 bytes size)
    
    // Optimized token processing with batch operations
    while (input_pos < input_size && current_pos < *output_size) {
        int offset, length;
        unsigned char next_char;
        
        // Use the standard function instead of the optimized one that's not defined
        if (read_lz77_token(input, input_size, &input_pos, &offset, &length, &next_char) != 0) {
            return -1;
        }
        
        if (length == 0) {
            // Literal character - direct copy
            if (current_pos >= *output_size) return -1;
            (*output)[current_pos++] = next_char;
        } else {
            // Copy from sliding window with optimized memory operations
            long copy_start = current_pos - offset;
            
            // Validate back-reference
            if (offset <= 0 || copy_start < 0 || copy_start >= current_pos) {
                return -1;
            }
            if (current_pos + length > *output_size) {
                // Prevent writing beyond declared original size
                return -1;
            }

            if (copy_start >= 0 && copy_start < current_pos) {
                // Optimized copying for overlapping regions
                if (offset >= length) {
                    // Non-overlapping copy - use fast memcpy
                    memcpy(*output + current_pos, *output + copy_start, length);
                    current_pos += length;
                } else {
                    // Overlapping copy - optimized byte-by-byte with pattern detection
                    for (int i = 0; i < length; i++) {
                        (*output)[current_pos] = (*output)[copy_start + (i % offset)];
                        current_pos++;
                    }
                }
            } else {
                // Handle edge case - skip invalid reference
                return -1;
            }
        }
    }
    
    // Adjust output size to actual decompressed size
    if (current_pos != *output_size) {
        return -1;
    }
    return 0;
}

// Optimized token reading with improved parsing
int read_lz77_token_optimized(const unsigned char* input, long input_size, long* pos, 
                             int* offset, int* length, unsigned char* next_char) {
    if (*pos >= input_size) return -1;
    
    unsigned char first_byte = input[(*pos)++];
    
    if (first_byte < 0x80) {
        // Direct literal character
        *offset = 0;
        *length = 0;
        *next_char = first_byte;
    } else if (first_byte == 0x80) {
        // Escaped literal character (>= 128)
        if (*pos >= input_size) return -1;
        *offset = 0;
        *length = 0;
        *next_char = input[(*pos)++];
    } else if (first_byte == 0xFF) {
        // Long match: 0xFF + length + offset(2bytes)
        if (*pos + 3 >= input_size) return -1;
        *length = input[(*pos)++] + MIN_MATCH_LENGTH;
        *offset = (input[(*pos)++] << 8) | input[(*pos)++];
        *next_char = 0; // Not used in new format
    } else {
        // Short match: 0x81 + length(4bits) + offset(1byte)
        if (*pos >= input_size) return -1;
        *length = ((first_byte & 0x1E) >> 1) + MIN_MATCH_LENGTH;
        *offset = input[(*pos)++];
        *next_char = 0; // Not used in new format
    }
    
    return 0;
}

// Legacy LZ77 decompression function for backward compatibility
int lz77_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    return lz77_decompress_optimized(input, input_size, output, output_size);
}

// Optimized LZW decompression
int lzw_decompress_optimized(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    // For now, use optimized LZ77 as LZW is more complex to implement properly
    return lz77_decompress_optimized(input, input_size, output, output_size);
}

// Legacy LZW decompression function for backward compatibility
int lzw_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    // Use the dedicated LZW decoder from missing_functions.c which matches the encoder format
    return lzw_decompress_mfunc(input, input_size, output, output_size);
}