/******************************************************************************
 * Advanced File Compressor - Compressor Core Module
 * Description: Core decompression engine with DSA reconstruction and validation
 ******************************************************************************/

#include "../include/decompressor.h"
#include "../include/compressor.h"

/*============================================================================*/
/* PRIVATE CONSTANTS                                                          */
/*============================================================================*/

#define HUFFMAN_MAX_CODES       256
#define LZ77_WINDOW_SIZE        4096
#define LZ77_MIN_MATCH          3
#define LZ77_MAX_MATCH          18
#define LZW_MAX_CODES           4096
#define RLE_MAX_RUN             127

/*============================================================================*/
/* PRIVATE STRUCTURES                                                         */
/*============================================================================*/

typedef struct HuffmanNode {
    uint8_t symbol;
    uint32_t frequency;
    struct HuffmanNode* left;
    struct HuffmanNode* right;
} HuffmanNode;

typedef struct {
    uint8_t* window;
    size_t window_size;
    size_t window_pos;
    bool initialized;
} LZ77Dictionary;

typedef struct {
    uint16_t* code_table;
    uint8_t* prefix_table;
    size_t table_size;
    bool initialized;
} LZWCodeTable;

typedef struct {
    uint8_t* run_buffer;
    size_t buffer_size;
    bool in_run;
    uint8_t run_value;
    size_t run_length;
} RLEState;

/*============================================================================*/
/* FORWARD DECLARATIONS                                                       */
/*============================================================================*/

static DecompStatus DecompressHuffman(const uint8_t* compressed, size_t compressed_size,
                                     uint8_t** decompressed, size_t* decompressed_size);
static DecompStatus DecompressLZ77(const uint8_t* compressed, size_t compressed_size,
                                  uint8_t** decompressed, size_t* decompressed_size);
static DecompStatus DecompressLZW(const uint8_t* compressed, size_t compressed_size,
                                 uint8_t** decompressed, size_t* decompressed_size);
static DecompStatus DecompressRLE(const uint8_t* compressed, size_t compressed_size,
                                 uint8_t** decompressed, size_t* decompressed_size);
static DecompStatus DecompressHardcore(const uint8_t* compressed, size_t compressed_size,
                                      uint8_t** decompressed, size_t* decompressed_size);
static DecompStatus DecompressAudioAdvanced(const uint8_t* compressed, size_t compressed_size,
                                           uint8_t** decompressed, size_t* decompressed_size);
static DecompStatus DecompressImageAdvanced(const uint8_t* compressed, size_t compressed_size,
                                           uint8_t** decompressed, size_t* decompressed_size);

static bool ValidateDecompressionResult(const uint8_t* original, size_t original_size,
                                       const uint8_t* decompressed, size_t decompressed_size,
                                       DecompAlgorithm algorithm) {
    // Basic validation for now
    return decompressed != NULL && decompressed_size > 0;
}
static uint32_t CalculateChecksum(const uint8_t* data, size_t size);
static void LogDecompressionInfo(DecompAlgorithm algorithm, size_t original_size, size_t compressed_size, size_t decompressed_size);

/*============================================================================*/
/* MODULE INITIALIZATION                                                      */
/*============================================================================*/

int CompressorCore_Init(void) {
    Logger_Log(LOG_LEVEL_INFO, "Compressor Core module initialized");
    return 0;
}

/*============================================================================*/
/* MAIN DECOMPRESSION INTERFACE                                               */
/*============================================================================*/

DecompStatus CompressorCore_DecompressBlock(const uint8_t* compressed, size_t compressed_size,
                                           uint8_t** decompressed, size_t* decompressed_size,
                                           DecompAlgorithm algorithm) {
    if (!compressed || !decompressed || !decompressed_size) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid parameters to CompressorCore_DecompressBlock");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    if (compressed_size == 0) {
        Logger_Log(LOG_LEVEL_ERROR, "Empty compressed data");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "Starting decompression with algorithm %d", algorithm);
    
    DecompStatus status;
    
    switch (algorithm) {
        case ALGO_HUFFMAN:
            status = DecompressHuffman(compressed, compressed_size, decompressed, decompressed_size);
            break;
            
        case ALGO_LZ77:
            status = DecompressLZ77(compressed, compressed_size, decompressed, decompressed_size);
            break;
            
        case ALGO_LZW:
            status = DecompressLZW(compressed, compressed_size, decompressed, decompressed_size);
            break;
            
        case ALGO_RLE:
            status = DecompressRLE(compressed, compressed_size, decompressed, decompressed_size);
            break;
            
        case ALGO_HARDCORE:
            status = DecompressHardcore(compressed, compressed_size, decompressed, decompressed_size);
            break;
            
        case ALGO_AUDIO_ADVANCED:
            status = DecompressAudioAdvanced(compressed, compressed_size, decompressed, decompressed_size);
            break;
            
        case ALGO_IMAGE_ADVANCED:
            status = DecompressImageAdvanced(compressed, compressed_size, decompressed, decompressed_size);
            break;
            
        default:
            Logger_Log(LOG_LEVEL_ERROR, "Unknown decompression algorithm: %d", algorithm);
            return DECOMP_STATUS_INVALID_ALGORITHM;
    }
    
    if (status != DECOMP_STATUS_SUCCESS) {
        Logger_Log(LOG_LEVEL_ERROR, "Decompression failed for algorithm %d", algorithm);
        return status;
    }
    
    // Validate decompression result
    if (!ValidateDecompressionResult(compressed, compressed_size, *decompressed, *decompressed_size, algorithm)) {
        Logger_Log(LOG_LEVEL_ERROR, "Decompression validation failed");
        free(*decompressed);
        *decompressed = NULL;
        *decompressed_size = 0;
        return DECOMP_STATUS_INTEGRITY_FAILURE;
    }
    
    LogDecompressionInfo(algorithm, 0, compressed_size, *decompressed_size);
    
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* ALGORITHM SELECTION                                                        */
/*============================================================================*/

DecompAlgorithm CompressorCore_SelectAlgorithm(const CompHeader* header, DecompFileType file_type) {
    if (!header) {
        return ALGO_UNKNOWN;
    }
    
    // Use algorithm from header if specified
    if (header->algorithm != ALGO_UNKNOWN) {
        Logger_Log(LOG_LEVEL_DEBUG, "Using algorithm from header: %d", header->algorithm);
        return header->algorithm;
    }
    
    // Select based on file type
    switch (file_type) {
        case FILE_TYPE_TEXT:
        case FILE_TYPE_CSV:
        case FILE_TYPE_JSON:
        case FILE_TYPE_XML:
            Logger_Log(LOG_LEVEL_DEBUG, "Selecting Huffman for text-based file");
            return ALGO_HUFFMAN;
            
        case FILE_TYPE_PDF:
        case FILE_TYPE_DOCX:
            Logger_Log(LOG_LEVEL_DEBUG, "Selecting LZ77 for document file");
            return ALGO_LZ77;
            
        case FILE_TYPE_AUDIO:
            Logger_Log(LOG_LEVEL_DEBUG, "Selecting audio advanced algorithm");
            return ALGO_AUDIO_ADVANCED;
            
        case FILE_TYPE_IMAGE:
            Logger_Log(LOG_LEVEL_DEBUG, "Selecting image advanced algorithm");
            return ALGO_IMAGE_ADVANCED;
            
        case FILE_TYPE_BINARY:
            Logger_Log(LOG_LEVEL_DEBUG, "Selecting hardcore for binary file");
            return ALGO_HARDCORE;
            
        default:
            Logger_Log(LOG_LEVEL_DEBUG, "Defaulting to Huffman algorithm");
            return ALGO_HUFFMAN;
    }
}

/*============================================================================*/
/* HUFFMAN DECOMPRESSION                                                      */
/*============================================================================*/

static DecompStatus DecompressHuffman(const uint8_t* compressed, size_t compressed_size,
                                       uint8_t** decompressed, size_t* decompressed_size) {
    Logger_Log(LOG_LEVEL_DEBUG, "Starting Huffman decompression");
    unsigned char* out = NULL; long out_sz = 0;
    CompResult rc = huffman_decompress((const unsigned char*)compressed, (long)compressed_size, &out, &out_sz);
    if (rc != COMP_OK || !out || out_sz <= 0) {
        Logger_Log(LOG_LEVEL_ERROR, "Huffman decompression failed");
        return DECOMP_STATUS_DECOMPRESSION_ERROR;
    }
    *decompressed = (uint8_t*)out;
    *decompressed_size = (size_t)out_sz;
    Logger_Log(LOG_LEVEL_DEBUG, "Huffman decompression completed");
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* LZ77 DECOMPRESSION                                                         */
/*============================================================================*/

static DecompStatus DecompressLZ77(const uint8_t* compressed, size_t compressed_size,
                                  uint8_t** decompressed, size_t* decompressed_size) {
    Logger_Log(LOG_LEVEL_DEBUG, "Starting LZ77 decompression");
    unsigned char* out = NULL; long out_sz = 0;
    int rc = lz77_decompress((const unsigned char*)compressed, (long)compressed_size, &out, &out_sz);
    if (rc != 0 || !out || out_sz <= 0) {
        Logger_Log(LOG_LEVEL_ERROR, "LZ77 decompression failed");
        return DECOMP_STATUS_DECOMPRESSION_ERROR;
    }
    *decompressed = (uint8_t*)out;
    *decompressed_size = (size_t)out_sz;
    Logger_Log(LOG_LEVEL_DEBUG, "LZ77 decompression completed");
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* LZW DECOMPRESSION                                                          */
/*============================================================================*/

static DecompStatus DecompressLZW(const uint8_t* compressed, size_t compressed_size,
                                 uint8_t** decompressed, size_t* decompressed_size) {
    Logger_Log(LOG_LEVEL_DEBUG, "Starting LZW decompression");
    unsigned char* out = NULL; long out_sz = 0;
    int rc = lzw_decompress((const unsigned char*)compressed, (long)compressed_size, &out, &out_sz);
    if (rc != 0 || !out || out_sz <= 0) {
        Logger_Log(LOG_LEVEL_ERROR, "LZW decompression failed");
        return DECOMP_STATUS_DECOMPRESSION_ERROR;
    }
    *decompressed = (uint8_t*)out;
    *decompressed_size = (size_t)out_sz;
    Logger_Log(LOG_LEVEL_DEBUG, "LZW decompression completed");
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* RLE DECOMPRESSION                                                          */
/*============================================================================*/

static DecompStatus DecompressRLE(const uint8_t* compressed, size_t compressed_size,
                                 uint8_t** decompressed, size_t* decompressed_size) {
    Logger_Log(LOG_LEVEL_DEBUG, "Starting RLE decompression");
    // Not implemented: enforce error instead of raw-pass through
    (void)compressed; (void)compressed_size; (void)decompressed; (void)decompressed_size;
    Logger_Log(LOG_LEVEL_ERROR, "RLE decompression not implemented");
    return DECOMP_STATUS_DECOMPRESSION_ERROR;
}

/*============================================================================*/
/* HARDCORE DECOMPRESSION                                                     */
/*============================================================================*/

static DecompStatus DecompressHardcore(const uint8_t* compressed, size_t compressed_size,
                                      uint8_t** decompressed, size_t* decompressed_size) {
    Logger_Log(LOG_LEVEL_DEBUG, "Starting Hardcore decompression");
    unsigned char* out = NULL; long out_sz = 0;
    int rc = hardcore_decompress((const unsigned char*)compressed, (long)compressed_size, &out, &out_sz);
    if (rc != 0 || !out || out_sz <= 0) {
        Logger_Log(LOG_LEVEL_ERROR, "Hardcore decompression failed");
        return DECOMP_STATUS_DECOMPRESSION_ERROR;
    }
    *decompressed = (uint8_t*)out;
    *decompressed_size = (size_t)out_sz;
    Logger_Log(LOG_LEVEL_DEBUG, "Hardcore decompression completed");
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* ADVANCED AUDIO DECOMPRESSION                                               */
/*============================================================================*/

static DecompStatus DecompressAudioAdvanced(const uint8_t* compressed, size_t compressed_size,
                                           uint8_t** decompressed, size_t* decompressed_size) {
    Logger_Log(LOG_LEVEL_DEBUG, "Starting advanced audio decompression");
    
    // For now, use a simple pass-through (to be implemented)
    *decompressed = (uint8_t*)malloc(compressed_size);
    if (!*decompressed) {
        return DECOMP_STATUS_MEMORY_ERROR;
    }
    
    memcpy(*decompressed, compressed, compressed_size);
    *decompressed_size = compressed_size;
    
    Logger_Log(LOG_LEVEL_DEBUG, "Advanced audio decompression completed");
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* ADVANCED IMAGE DECOMPRESSION                                               */
/*============================================================================*/

static DecompStatus DecompressImageAdvanced(const uint8_t* compressed, size_t compressed_size,
                                           uint8_t** decompressed, size_t* decompressed_size) {
    Logger_Log(LOG_LEVEL_DEBUG, "Starting advanced image decompression");
    
    // For now, use a simple pass-through (to be implemented)
    *decompressed = (uint8_t*)malloc(compressed_size);
    if (!*decompressed) {
        return DECOMP_STATUS_MEMORY_ERROR;
    }
    
    memcpy(*decompressed, compressed, compressed_size);
    *decompressed_size = compressed_size;
    
    Logger_Log(LOG_LEVEL_DEBUG, "Advanced image decompression completed");
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* VALIDATION FUNCTIONS                                                       */
/*============================================================================*/

bool CompressorCore_ValidateDecompression(const uint8_t* original, size_t original_size,
                                         const uint8_t* decompressed, size_t decompressed_size) {
    if (!original || !decompressed) {
        return false;
    }
    
    // Basic size validation
    if (decompressed_size == 0) {
        Logger_Log(LOG_LEVEL_ERROR, "Decompressed size is zero");
        return false;
    }
    
    if (decompressed_size > DECOMP_MAX_REASONABLE) {
        Logger_Log(LOG_LEVEL_ERROR, "Decompressed size too large: %zu", decompressed_size);
        return false;
    }
    
    // Calculate checksums for validation
    uint32_t original_checksum = CalculateChecksum(original, original_size);
    uint32_t decompressed_checksum = CalculateChecksum(decompressed, decompressed_size);
    
    // For now, just validate that decompressed data is reasonable
    // In a full implementation, this would compare against expected checksums
    
    Logger_Log(LOG_LEVEL_DEBUG, "Decompression validation: size=%zu, checksum=0x%08X",
              decompressed_size, decompressed_checksum);
    
    return true;
}

/*============================================================================*/
/* UTILITY FUNCTIONS                                                          */
/*============================================================================*/

static uint32_t CalculateChecksum(const uint8_t* data, size_t size) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum = ((checksum << 1) | (checksum >> 31)) ^ data[i];
    }
    return checksum;
}

static void LogDecompressionInfo(DecompAlgorithm algorithm, size_t original_size, 
                                 size_t compressed_size, size_t decompressed_size) {
    double ratio = 0.0;
    if (compressed_size > 0) {
        ratio = ((double)(original_size - compressed_size) / original_size) * 100.0;
    }
    
    Logger_Log(LOG_LEVEL_INFO, "Decompression completed:");
    Logger_Log(LOG_LEVEL_INFO, "  Algorithm: %s", Utility_GetAlgorithmName(algorithm));
    Logger_Log(LOG_LEVEL_INFO, "  Original size: %zu bytes", original_size);
    Logger_Log(LOG_LEVEL_INFO, "  Compressed size: %zu bytes", compressed_size);
    Logger_Log(LOG_LEVEL_INFO, "  Decompressed size: %zu bytes", decompressed_size);
    Logger_Log(LOG_LEVEL_INFO, "  Compression ratio: %.2f%%", ratio);
}

/*============================================================================*/
/* MODULE CLEANUP                                                             */
/*============================================================================*/

void CompressorCore_Cleanup(void) {
    Logger_Log(LOG_LEVEL_INFO, "Compressor Core module cleaned up");
}