/******************************************************************************
 * Advanced File Compressor - Utility Module
 * Description: Common utility functions for decompression operations
 ******************************************************************************/

#include "../include/decompressor.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#endif

/*============================================================================*/
/* PRIVATE CONSTANTS                                                          */
/*============================================================================*/

#define CHECKSUM_POLYNOMIAL     0xEDB88320L
#define MAX_STATUS_STRING       256
#define MAX_SIZE_STRING         64

/*============================================================================*/
/* FORWARD DECLARATIONS                                                       */
/*============================================================================*/

static uint32_t crc32_table[256];
static bool crc32_initialized = false;

static void InitializeCRC32(void);
static const char* GetAlgorithmNameInternal(DecompAlgorithm algorithm);
static const char* GetStatusDescriptionInternal(DecompStatus status);
static const char* GetFileTypeNameInternal(DecompFileType file_type);

/*============================================================================*/
/* CHECKSUM CALCULATION                                                       */
/*============================================================================*/

uint32_t Utility_CalculateChecksum(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }
    
    if (!crc32_initialized) {
        InitializeCRC32();
    }
    
    uint32_t crc = 0xFFFFFFFFL;
    
    for (size_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFFL;
}

static void InitializeCRC32(void) {
    for (int i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CHECKSUM_POLYNOMIAL;
            } else {
                crc = crc >> 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = true;
}

/*============================================================================*/
/* FILE TYPE DETECTION                                                        */
/*============================================================================*/

DecompFileType Utility_DetectFileType(const uint8_t* data, size_t size) {
    if (!data || size < 4) {
        return FILE_TYPE_BINARY;
    }
    
    // Check for text files (printable ASCII characters)
    bool is_text = true;
    size_t sample_size = (size < 1024) ? size : 1024;
    
    for (size_t i = 0; i < sample_size; i++) {
        uint8_t ch = data[i];
        if (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t' && ch != 0) {
            is_text = false;
            break;
        }
    }
    
    if (is_text) {
        // Enhanced text type detection
        if (size >= 2) {
            // Check for JSON
            if ((data[0] == '{' && data[size-1] == '}') || 
                (data[0] == '[' && data[size-1] == ']')) {
                return FILE_TYPE_JSON;
            }
            
            // Check for XML
            if (size >= 5 && memcmp(data, "<?xml", 5) == 0) {
                return FILE_TYPE_XML;
            }
            
            // Check for CSV (simple heuristic)
            int comma_count = 0;
            int newline_count = 0;
            for (size_t i = 0; i < sample_size && i < 100; i++) {
                if (data[i] == ',') comma_count++;
                if (data[i] == '\n') newline_count++;
            }
            if (comma_count > 2 && newline_count > 0) {
                return FILE_TYPE_CSV;
            }
        }
        
        return FILE_TYPE_TEXT;
    }
    
    // Binary file detection
    if (size >= 4) {
        // PDF
        if ((size >= 7 && memcmp(data, "%PDF-", 5) == 0) || memcmp(data, "%PDF", 4) == 0) {
            return FILE_TYPE_PDF;
        }
        
        // PNG 24-bit focus
        if (size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
            return FILE_TYPE_PNG;
        }
        
        // JPEG
        if (size >= 2 && memcmp(data, "\xFF\xD8", 2) == 0) {
            return FILE_TYPE_IMAGE;
        }
        
        // BMP 24-bit: DIB size 40 and bpp=24
        if (size >= 54 && memcmp(data, "BM", 2) == 0) {
            if (*(const uint32_t*)(data + 14) == 40 && *(const uint16_t*)(data + 28) == 24) {
                return FILE_TYPE_BMP;
            }
            return FILE_TYPE_IMAGE;
        }
        // TGA 24-bit
        if ((size >= 18 && data[2] == 2 && data[16] == 24) ||
            (size >= 26 && memcmp(data + size - 18, "TRUEVISION-XFILE", 16) == 0)) {
            return FILE_TYPE_TGA;
        }
        
        // GIF
        if (size >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) {
            return FILE_TYPE_IMAGE;
        }
        
        // WAV
        if (size >= 4 && memcmp(data, "RIFF", 4) == 0) {
            return FILE_TYPE_AUDIO;
        }
        
        // MP3
        if (size >= 3 && (memcmp(data, "ID3", 3) == 0 || memcmp(data, "\xFF\xFB", 2) == 0)) {
            return FILE_TYPE_AUDIO;
        }
        
        // DOCX (ZIP format)
        if (size >= 4 && memcmp(data, "PK\x03\x04", 4) == 0) {
            return FILE_TYPE_DOCX;
        }
    }
    
    return FILE_TYPE_BINARY;
}

/*============================================================================*/
/* STRING FORMATTING FUNCTIONS                                                */
/*============================================================================*/

const char* Utility_GetAlgorithmName(DecompAlgorithm algorithm) {
    return GetAlgorithmNameInternal(algorithm);
}

static const char* GetAlgorithmNameInternal(DecompAlgorithm algorithm) {
    switch (algorithm) {
        case ALGO_HUFFMAN:        return "Huffman";
        case ALGO_LZ77:          return "LZ77";
        case ALGO_LZW:           return "LZW";
        case ALGO_RLE:           return "RLE";
        case ALGO_HARDCORE:      return "Hardcore";
        case ALGO_AUDIO_ADVANCED: return "Audio Advanced";
        case ALGO_IMAGE_ADVANCED: return "Image Advanced";
        default:                 return "Unknown";
    }
}

const char* Utility_GetStatusDescription(DecompStatus status) {
    return GetStatusDescriptionInternal(status);
}

static const char* GetStatusDescriptionInternal(DecompStatus status) {
    switch (status) {
        case DECOMP_STATUS_SUCCESS:           return "Success";
        case DECOMP_STATUS_INVALID_MAGIC:     return "Invalid magic number";
        case DECOMP_STATUS_UNSUPPORTED_VERSION: return "Unsupported version";
        case DECOMP_STATUS_CORRUPTED_HEADER:  return "Corrupted header";
        case DECOMP_STATUS_TRUNCATED_DATA:  return "Truncated data";
        case DECOMP_STATUS_INVALID_ALGORITHM: return "Invalid algorithm";
        case DECOMP_STATUS_DECOMPRESSION_ERROR: return "Decompression error";
        case DECOMP_STATUS_INTEGRITY_FAILURE: return "Integrity failure";
        case DECOMP_STATUS_MEMORY_ERROR:    return "Memory error";
        case DECOMP_STATUS_IO_ERROR:          return "I/O error";
        default:                              return "Unknown error";
    }
}

void Utility_FormatFileSize(uint64_t size, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    if (size < 1024) {
        snprintf(buffer, buffer_size, "%llu B", (unsigned long long)size);
    } else if (size < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.2f KB", size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.2f MB", size / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buffer_size, "%.2f GB", size / (1024.0 * 1024.0 * 1024.0));
    }
}

void Utility_GetTimestamp(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/*============================================================================*/
/* ALGORITHM-SPECIFIC UTILITIES                                               */
/*============================================================================*/

bool Utility_ValidateHuffmanTree(const void* tree_data, size_t size) {
    if (!tree_data || size == 0) {
        return false;
    }
    
    // Basic validation: check for reasonable size
    if (size < sizeof(uint32_t) || size > 1024 * 1024) { // Between 4 bytes and 1MB
        return false;
    }
    
    // Additional validation would be implemented based on specific Huffman tree format
    return true;
}

bool Utility_ValidateLZ77Dictionary(const void* dict_data, size_t size) {
    if (!dict_data || size == 0) {
        return false;
    }
    
    // Basic validation: check for reasonable window size
    if (size != 4096 && size != 8192 && size != 16384) { // Standard window sizes
        return false;
    }
    
    return true;
}

bool Utility_ValidateLZWTable(const void* table_data, size_t size) {
    if (!table_data || size == 0) {
        return false;
    }
    
    // Basic validation: check for reasonable table size
    if (size < 256 || size > 4096) { // Between 256 and 4096 entries
        return false;
    }
    
    return true;
}

/*============================================================================*/
/* MEMORY MANAGEMENT UTILITIES                                                  */
/*============================================================================*/

void* Utility_AllocAligned(size_t size, size_t alignment) {
    #ifdef _WIN32
        return _aligned_malloc(size, alignment);
    #else
        void* ptr = NULL;
        if (posix_memalign(&ptr, alignment, size) != 0) {
            return NULL;
        }
        return ptr;
    #endif
}

void Utility_FreeAligned(void* ptr) {
    if (!ptr) {
        return;
    }
    
    #ifdef _WIN32
        _aligned_free(ptr);
    #else
        free(ptr);
    #endif
}

/*============================================================================*/
/* ERROR HANDLING UTILITIES                                                   */
/*============================================================================*/

const char* Utility_GetLastErrorMessage(void) {
    #ifdef _WIN32
        static char error_message[256];
        DWORD error = GetLastError();
        
        if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                         NULL, error, 0, error_message, sizeof(error_message), NULL) == 0) {
            snprintf(error_message, sizeof(error_message), "Unknown error: %lu", error);
        }
        
        return error_message;
    #else
        return strerror(errno);
    #endif
}

bool Utility_IsRecoverableError(DecompStatus status) {
    switch (status) {
        case DECOMP_STATUS_INVALID_MAGIC:
        case DECOMP_STATUS_CORRUPTED_HEADER:
        case DECOMP_STATUS_TRUNCATED_DATA:
        case DECOMP_STATUS_DECOMPRESSION_ERROR:
            return true;
            
        case DECOMP_STATUS_SUCCESS:
        case DECOMP_STATUS_UNSUPPORTED_VERSION:
        case DECOMP_STATUS_INVALID_ALGORITHM:
        case DECOMP_STATUS_INTEGRITY_FAILURE:
        case DECOMP_STATUS_MEMORY_ERROR:
        case DECOMP_STATUS_IO_ERROR:
            return false;
            
        default:
            return false;
    }
}

/*============================================================================*/
/* BYTE ORDER CONVERSION FUNCTIONS                                            */
/*============================================================================*/

uint32_t be32toh(uint32_t value) {
#if defined(_WIN32)
    #if defined(_MSC_VER)
        return _byteswap_ulong(value);
    #else
        return __builtin_bswap32(value);
    #endif
#else
    return __builtin_bswap32(value);
#endif
}

uint64_t be64toh(uint64_t value) {
#if defined(_WIN32)
    #if defined(_MSC_VER)
        return _byteswap_uint64(value);
    #else
        return __builtin_bswap64(value);
    #endif
#else
    return __builtin_bswap64(value);
#endif
}

uint16_t be16toh(uint16_t value) {
#if defined(_WIN32)
    #if defined(_MSC_VER)
        return _byteswap_ushort(value);
    #else
        return __builtin_bswap16(value);
    #endif
#else
    return __builtin_bswap16(value);
#endif
}

/*============================================================================*/
/* PERFORMANCE MEASUREMENT                                                    */
/*============================================================================*/

double Utility_GetCPUTime(void) {
    #ifdef _WIN32
        FILETIME create_time, exit_time, kernel_time, user_time;
        if (GetProcessTimes(GetCurrentProcess(), &create_time, &exit_time, &kernel_time, &user_time)) {
            ULARGE_INTEGER kernel, user;
            kernel.LowPart = kernel_time.dwLowDateTime;
            kernel.HighPart = kernel_time.dwHighDateTime;
            user.LowPart = user_time.dwLowDateTime;
            user.HighPart = user_time.dwHighDateTime;
            
            return (double)(kernel.QuadPart + user.QuadPart) / 10000000.0; // Convert to seconds
        }
        return 0.0;
    #else
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            return (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec / 1000000.0 +
                   (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec / 1000000.0;
        }
        return 0.0;
    #endif
}

uint64_t Utility_GetPeakMemoryUsage(void) {
    #ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return pmc.PeakWorkingSetSize;
        }
        return 0;
    #else
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            return usage.ru_maxrss * 1024; // Convert from KB to bytes
        }
        return 0;
    #endif
}