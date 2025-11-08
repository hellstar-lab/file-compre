#include "../include/compressor.h"
#include <math.h>
#include <string.h>

// Audio compression levels
typedef enum {
    AUDIO_LEVEL_FAST = 1,
    AUDIO_LEVEL_NORMAL = 2,
    AUDIO_LEVEL_HIGH = 3,
    AUDIO_LEVEL_ULTRA = 4
} AudioCompressionLevel;

// WAV header structure
typedef struct {
    char riff[4];           // "RIFF"
    uint32_t file_size;     // File size - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmt_size;      // Format chunk size
    uint16_t format;        // Audio format (1 = PCM)
    uint16_t channels;      // Number of channels
    uint32_t sample_rate;   // Sample rate
    uint32_t byte_rate;     // Byte rate
    uint16_t block_align;   // Block align
    uint16_t bits_per_sample; // Bits per sample
    char data[4];           // "data"
    uint32_t data_size;     // Data size
} WAVHeader;

// Advanced audio compression using adaptive quantization and psychoacoustic modeling
int compress_audio_advanced(const unsigned char* input, long input_size, 
                           unsigned char** output, long* output_size, 
                           AudioCompressionLevel level) {
    if (!input || input_size < sizeof(WAVHeader)) return -1;
    
    WAVHeader* header = (WAVHeader*)input;
    
    // Validate WAV format
    if (strncmp(header->riff, "RIFF", 4) != 0 || 
        strncmp(header->wave, "WAVE", 4) != 0 ||
        header->format != 1) {
        return -1; // Not a valid PCM WAV file
    }
    
    const unsigned char* audio_data = input + sizeof(WAVHeader);
    long audio_data_size = header->data_size;
    
    // Calculate compression parameters based on level
    int window_size, overlap, quantization_bits;
    double quality_factor;
    
    switch (level) {
        case AUDIO_LEVEL_FAST:
            window_size = 512;
            overlap = 256;
            quantization_bits = 12;
            quality_factor = 0.7;
            break;
        case AUDIO_LEVEL_NORMAL:
            window_size = 1024;
            overlap = 512;
            quantization_bits = 10;
            quality_factor = 0.8;
            break;
        case AUDIO_LEVEL_HIGH:
            window_size = 2048;
            overlap = 1024;
            quantization_bits = 8;
            quality_factor = 0.9;
            break;
        case AUDIO_LEVEL_ULTRA:
            window_size = 4096;
            overlap = 2048;
            quantization_bits = 6;
            quality_factor = 0.95;
            break;
        default:
            return -1;
    }
    
    // Allocate output buffer
    long max_output_size = input_size + 1024; // Header + compressed data
    *output = (unsigned char*)malloc(max_output_size);
    *output_size = 0;
    
    // Write compression header
    (*output)[(*output_size)++] = 'A'; // Audio marker
    (*output)[(*output_size)++] = 'C'; // Compressed marker
    (*output)[(*output_size)++] = level; // Compression level
    (*output)[(*output_size)++] = 0; // Reserved
    
    // Copy original WAV header
    memcpy(*output + *output_size, header, sizeof(WAVHeader));
    *output_size += sizeof(WAVHeader);
    
    // Process audio in blocks with adaptive quantization
    long processed = 0;
    int16_t* samples = (int16_t*)audio_data;
    long num_samples = audio_data_size / sizeof(int16_t);
    
    while (processed < num_samples) {
        long block_size = (processed + window_size <= num_samples) ? 
                         window_size : (num_samples - processed);
        
        // Calculate block statistics for adaptive quantization
        double energy = 0.0;
        double max_amplitude = 0.0;
        
        for (long i = 0; i < block_size; i++) {
            double sample = (double)samples[processed + i];
            energy += sample * sample;
            if (fabs(sample) > max_amplitude) {
                max_amplitude = fabs(sample);
            }
        }
        
        energy = sqrt(energy / block_size);
        
        // Adaptive quantization based on psychoacoustic masking
        int effective_bits = quantization_bits;
        if (energy < max_amplitude * 0.1) {
            effective_bits = quantization_bits - 1; // Reduce bits for quiet sections
        } else if (energy > max_amplitude * 0.8) {
            effective_bits = quantization_bits + 1; // Increase bits for loud sections
        }
        
        // Quantize and compress block
        int quantization_levels = 1 << effective_bits;
        double scale_factor = quantization_levels / (2.0 * max_amplitude);
        
        // Write block header
        (*output)[(*output_size)++] = (block_size >> 8) & 0xFF;
        (*output)[(*output_size)++] = block_size & 0xFF;
        (*output)[(*output_size)++] = effective_bits;
        
        // Store scale factor (4 bytes)
        union { double d; uint64_t i; } scale_union;
        scale_union.d = scale_factor;
        for (int i = 0; i < 4; i++) {
            (*output)[(*output_size)++] = (scale_union.i >> (i * 8)) & 0xFF;
        }
        
        // Quantize and encode samples using differential encoding
        int16_t prev_quantized = 0;
        for (long i = 0; i < block_size; i++) {
            int16_t quantized = (int16_t)(samples[processed + i] * scale_factor);
            int16_t diff = quantized - prev_quantized;
            
            // Variable-length encoding for differences
            if (diff >= -127 && diff <= 127) {
                (*output)[(*output_size)++] = (unsigned char)(diff + 128);
            } else {
                (*output)[(*output_size)++] = 0; // Escape code
                (*output)[(*output_size)++] = (diff >> 8) & 0xFF;
                (*output)[(*output_size)++] = diff & 0xFF;
            }
            
            prev_quantized = quantized;
        }
        
        processed += block_size - overlap;
    }
    
    // Update compressed data size in header
    WAVHeader* output_header = (WAVHeader*)(*output + 4);
    output_header->data_size = *output_size - sizeof(WAVHeader) - 4;
    output_header->file_size = *output_size - 8;
    
    return 0;
}

// Audio decompression
int decompress_audio_advanced(const unsigned char* input, long input_size,
                            unsigned char** output, long* output_size) {
    if (!input || input_size < 8) return -1;
    
    // Check audio compression marker
    if (input[0] != 'A' || input[1] != 'C') return -1;
    
    AudioCompressionLevel level = (AudioCompressionLevel)input[2];
    const WAVHeader* header = (const WAVHeader*)(input + 4);
    
    // Allocate output buffer
    *output_size = sizeof(WAVHeader) + header->data_size * 2; // Estimate
    *output = (unsigned char*)malloc(*output_size);
    
    // Copy WAV header
    memcpy(*output, header, sizeof(WAVHeader));
    
    // Decompress audio data
    long input_pos = 4 + sizeof(WAVHeader);
    long output_pos = sizeof(WAVHeader);
    int16_t* output_samples = (int16_t*)(*output + output_pos);
    long sample_count = 0;
    
    while (input_pos < input_size) {
        // Read block header
        if (input_pos + 7 >= input_size) break;
        
        long block_size = (input[input_pos] << 8) | input[input_pos + 1];
        int effective_bits = input[input_pos + 2];
        input_pos += 3;
        
        // Read scale factor
        union { double d; uint64_t i; } scale_union;
        scale_union.i = 0;
        for (int i = 0; i < 4; i++) {
            scale_union.i |= ((uint64_t)input[input_pos++]) << (i * 8);
        }
        double scale_factor = scale_union.d;
        
        // Decompress samples
        int16_t prev_quantized = 0;
        for (long i = 0; i < block_size && input_pos < input_size; i++) {
            int16_t diff;
            
            if (input[input_pos] == 0) {
                // Escape code - read 16-bit difference
                input_pos++;
                if (input_pos + 1 >= input_size) break;
                diff = (input[input_pos] << 8) | input[input_pos + 1];
                input_pos += 2;
            } else {
                // 8-bit difference
                diff = (int16_t)input[input_pos++] - 128;
            }
            
            int16_t quantized = prev_quantized + diff;
            output_samples[sample_count++] = (int16_t)(quantized / scale_factor);
            prev_quantized = quantized;
        }
    }
    
    // Update actual output size
    *output_size = sizeof(WAVHeader) + sample_count * sizeof(int16_t);
    WAVHeader* output_header = (WAVHeader*)*output;
    output_header->data_size = sample_count * sizeof(int16_t);
    output_header->file_size = *output_size - 8;
    
    return 0;
}

// Main audio compression interface
int audio_compress(const unsigned char* input, long input_size, 
                  unsigned char** output, long* output_size, int level) {
    if (level < 1 || level > 4) level = 2; // Default to normal
    return compress_audio_advanced(input, input_size, output, output_size, 
                                 (AudioCompressionLevel)level);
}

// Main audio decompression interface
int audio_decompress(const unsigned char* input, long input_size,
                    unsigned char** output, long* output_size) {
    return decompress_audio_advanced(input, input_size, output, output_size);
}