/******************************************************************************
 * Advanced File Compressor - Parser Module
 * Description: COMP container header parsing and validation with error recovery
 ******************************************************************************/

#include "../include/decompressor.h"

/*============================================================================*/
/* PRIVATE CONSTANTS                                                          */
/*============================================================================*/

#define COMP_MAGIC_SIZE         4
#define COMP_HEADER_SIZE_V3     64
#define COMP_HEADER_SIZE_V4     128
#define COMP_MIN_FILE_SIZE      16

/*============================================================================*/
/* PRIVATE STRUCTURES                                                         */
/*============================================================================*/

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  algorithm;
    uint64_t original_size;
    uint64_t compressed_size;
    uint32_t checksum;
    char     original_extension[64];
    time_t   timestamp;
    uint32_t block_count;
    uint32_t flags;
} InternalCompHeader;

/*============================================================================*/
/* FORWARD DECLARATIONS                                                       */
/*============================================================================*/

static DecompStatus ParseV3Header(FILE* file, InternalCompHeader* header);
static DecompStatus ParseV4Header(FILE* file, InternalCompHeader* header);
static bool ValidateHeaderIntegrity(const InternalCompHeader* header);
static DecompStatus DetectCorruptionType(const uint8_t* data, size_t size);
static void LogHeaderInfo(const InternalCompHeader* header);

/*============================================================================*/
/* MODULE INITIALIZATION                                                      */
/*============================================================================*/

int Parser_Init(void) {
    Logger_Log(LOG_LEVEL_INFO, "Parser module initialized");
    return 0;
}

/*============================================================================*/
/* HEADER PARSING FUNCTIONS                                                   */
/*============================================================================*/

DecompStatus Parser_ParseHeader(FILE* file, CompHeader* header) {
    if (!file || !header) {
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "Starting header parsing");
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size < COMP_MIN_FILE_SIZE) {
        Logger_Log(LOG_LEVEL_ERROR, "File too small for COMP header: %ld bytes", file_size);
        return DECOMP_STATUS_CORRUPTED_HEADER;
    }
    
    // Read magic number
    uint32_t magic;
    if (fread(&magic, sizeof(uint32_t), 1, file) != 1) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read magic number");
        return DECOMP_STATUS_IO_ERROR;
    }
    
    // Convert to host byte order
    magic = le32toh(magic);
    
    // Validate magic
    if (magic != 0x504D4F43) { // "COMP" in little-endian
        Logger_Log(LOG_LEVEL_ERROR, "Invalid magic number: 0x%08X", magic);
        return DECOMP_STATUS_INVALID_MAGIC;
    }
    
    // Read version
    uint8_t version;
    if (fread(&version, sizeof(uint8_t), 1, file) != 1) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read version");
        return DECOMP_STATUS_IO_ERROR;
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "Detected COMP container version %d", version);
    
    // Parse based on version
    InternalCompHeader internal_header = {0};
    internal_header.magic = magic;
    internal_header.version = version;
    
    DecompStatus status;
    switch (version) {
        case 3:
            status = ParseV3Header(file, &internal_header);
            break;
        case 4:
            status = ParseV4Header(file, &internal_header);
            break;
        default:
            Logger_Log(LOG_LEVEL_ERROR, "Unsupported COMP version: %d", version);
            return DECOMP_STATUS_UNSUPPORTED_VERSION;
    }
    
    if (status != DECOMP_STATUS_SUCCESS) {
        return status;
    }
    
    // Validate header integrity
    if (!ValidateHeaderIntegrity(&internal_header)) {
        Logger_Log(LOG_LEVEL_ERROR, "Header integrity validation failed");
        return DECOMP_STATUS_CORRUPTED_HEADER;
    }
    
    // Copy to output structure
    memcpy(header, &internal_header, sizeof(CompHeader));
    
    LogHeaderInfo(&internal_header);
    
    Logger_Log(LOG_LEVEL_INFO, "Header parsing completed successfully");
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* V3 HEADER PARSING                                                          */
/*============================================================================*/

static DecompStatus ParseV3Header(FILE* file, InternalCompHeader* header) {
    Logger_Log(LOG_LEVEL_DEBUG, "Parsing V3 header");
    
    // Read algorithm
    if (fread(&header->algorithm, sizeof(uint8_t), 1, file) != 1) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read algorithm");
        return DECOMP_STATUS_IO_ERROR;
    }
    
    // Read original size (8 bytes)
    if (fread(&header->original_size, sizeof(uint64_t), 1, file) != 1) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read original size");
        return DECOMP_STATUS_IO_ERROR;
    }
    header->original_size = le64toh(header->original_size);
    
    // Read compressed size (8 bytes)
    if (fread(&header->compressed_size, sizeof(uint64_t), 1, file) != 1) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read compressed size");
        return DECOMP_STATUS_IO_ERROR;
    }
    header->compressed_size = le64toh(header->compressed_size);
    
    // Read checksum (4 bytes)
    if (fread(&header->checksum, sizeof(uint32_t), 1, file) != 1) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read checksum");
        return DECOMP_STATUS_IO_ERROR;
    }
    header->checksum = le32toh(header->checksum);
    
    // Read original extension (64 bytes)
    if (fread(header->original_extension, 64, 1, file) != 1) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read original extension");
        return DECOMP_STATUS_IO_ERROR;
    }
    header->original_extension[63] = '\0'; // Ensure null termination
    
    // Read timestamp (8 bytes)
    if (fread(&header->timestamp, sizeof(time_t), 1, file) != 1) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read timestamp");
        return DECOMP_STATUS_IO_ERROR;
    }
    header->timestamp = le64toh(header->timestamp);
    
    header->block_count = 1; // V3 is single-block
    header->flags = 0;
    
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* V4 HEADER PARSING                                                          */
/*============================================================================*/

static DecompStatus ParseV4Header(FILE* file, InternalCompHeader* header) {
    Logger_Log(LOG_LEVEL_DEBUG, "Parsing V4 header (64-byte layout)");
    
    // Read algorithm (blockwise), then level and file type (discarded)
    uint8_t algo = 0, level = 0, ftype = 0;
    if (fread(&algo, sizeof(uint8_t), 1, file) != 1) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read algorithm");
        return DECOMP_STATUS_IO_ERROR;
    }
    if (fread(&level, sizeof(uint8_t), 1, file) != 1) { return DECOMP_STATUS_IO_ERROR; }
    if (fread(&ftype, sizeof(uint8_t), 1, file) != 1) { return DECOMP_STATUS_IO_ERROR; }
    header->algorithm = algo;

    // Original size: 8 bytes big-endian
    uint8_t be64[8];
    if (fread(be64, 1, 8, file) != 8) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read original size");
        return DECOMP_STATUS_IO_ERROR;
    }
    uint64_t orig = 0; for (int i = 0; i < 8; i++) orig = (orig << 8) | be64[i];
    header->original_size = orig;

    // Compressed total: 8 bytes big-endian (may be zero placeholder)
    if (fread(be64, 1, 8, file) != 8) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read compressed total");
        return DECOMP_STATUS_IO_ERROR;
    }
    uint64_t comp_total = 0; for (int i = 0; i < 8; i++) comp_total = (comp_total << 8) | be64[i];
    header->compressed_size = comp_total;

    // Block count: 4 bytes big-endian
    uint8_t be32[4];
    if (fread(be32, 1, 4, file) != 4) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read block count");
        return DECOMP_STATUS_IO_ERROR;
    }
    header->block_count = (be32[0] << 24) | (be32[1] << 16) | (be32[2] << 8) | be32[3];

    // Marker: 'BLK4'
    char marker[4];
    if (fread(marker, 1, 4, file) != 4) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read BLK4 marker");
        return DECOMP_STATUS_IO_ERROR;
    }
    if (!(marker[0] == 'B' && marker[1] == 'L' && marker[2] == 'K' && marker[3] == '4')) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid BLK4 marker");
        return DECOMP_STATUS_CORRUPTED_HEADER;
    }

    // Skip remaining reserved bytes to reach end of 64-byte header
    long bytes_read = 1 + 1 + 1 + 8 + 8 + 4 + 4; // algo+level+ftype + sizes + count + marker
    long to_skip = 64 - bytes_read;
    if (to_skip > 0) { fseek(file, to_skip, SEEK_CUR); }

    // No checksum/extension/timestamp/flags in 64-byte v4 header
    header->checksum = 0;
    memset(header->original_extension, 0, sizeof(header->original_extension));
    header->timestamp = 0;
    header->flags = 0;
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* BLOCK METADATA PARSING                                                     */
/*============================================================================*/

DecompStatus Parser_ParseBlockMetadata(FILE* file, BlockMetadata* blocks, int* block_count) {
    if (!file || !blocks || !block_count) {
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "Parsing block metadata");
    
    // Read block table header: 'BTAB' + count (big-endian)
    char tmagic[4];
    if (fread(tmagic, 1, 4, file) != 4) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read block table magic");
        return DECOMP_STATUS_IO_ERROR;
    }
    if (!(tmagic[0] == 'B' && tmagic[1] == 'T' && tmagic[2] == 'A' && tmagic[3] == 'B')) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid block table magic");
        return DECOMP_STATUS_CORRUPTED_HEADER;
    }
    uint8_t be32b[4];
    if (fread(be32b, 1, 4, file) != 4) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read block count");
        return DECOMP_STATUS_IO_ERROR;
    }
    uint32_t num_blocks = (be32b[0] << 24) | (be32b[1] << 16) | (be32b[2] << 8) | be32b[3];
    
    if (num_blocks > DECOMP_MAX_BLOCKS) {
        Logger_Log(LOG_LEVEL_ERROR, "Too many blocks: %u (max: %d)", num_blocks, DECOMP_MAX_BLOCKS);
        return DECOMP_STATUS_CORRUPTED_HEADER;
    }
    
    *block_count = num_blocks;
    
    // Read block descriptors: 10 bytes per block
    for (uint32_t i = 0; i < num_blocks; i++) {
        BlockMetadata* block = &blocks[i];
        uint8_t desc[10];
        if (fread(desc, 1, 10, file) != 10) {
            Logger_Log(LOG_LEVEL_ERROR, "Failed to read block descriptor %u", i);
            return DECOMP_STATUS_IO_ERROR;
        }
        block->block_id = i;
        block->algorithm = desc[0];
        uint32_t orig32 = (desc[2] << 24) | (desc[3] << 16) | (desc[4] << 8) | desc[5];
        uint32_t comp32 = (desc[6] << 24) | (desc[7] << 16) | (desc[8] << 8) | desc[9];
        block->original_size = orig32;
        block->compressed_size = comp32;
        block->checksum = 0;
        block->data_offset = 0; // will be computed below
        Logger_Log(LOG_LEVEL_TRACE, "Block %u: orig=%lu, comp=%lu, algo=%u",
                   i, block->original_size, block->compressed_size, block->algorithm);
    }

    // Compute data offsets: immediately after descriptors
    long base_offset = ftell(file);
    long data_offset = base_offset;
    for (int i = 0; i < *block_count; i++) {
        blocks[i].data_offset = data_offset;
        data_offset += (long)blocks[i].compressed_size;
    }

    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* FORMAT DETECTION                                                           */
/*============================================================================*/

DecompStatus Parser_DetectFormat(const char* filepath, uint32_t* magic, uint8_t* version) {
    if (!filepath || !magic || !version) {
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to open file: %s", filepath);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    // Read magic number
    if (fread(magic, sizeof(uint32_t), 1, file) != 1) {
        fclose(file);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    *magic = le32toh(*magic);
    
    // Read version if magic is valid
    if (*magic == 0x504D4F43) { // "COMP"
        if (fread(version, sizeof(uint8_t), 1, file) == 1) {
            Logger_Log(LOG_LEVEL_DEBUG, "Detected COMP format v%d", *version);
        } else {
            *version = 0;
        }
    } else if (*magic == 0x01EFAD) { // Hardcore magic (little-endian)
        *version = 0xFF; // Special version for Hardcore
        Logger_Log(LOG_LEVEL_DEBUG, "Detected Hardcore format");
    } else {
        Logger_Log(LOG_LEVEL_WARNING, "Unknown format magic: 0x%08X", *magic);
        *version = 0;
    }
    
    fclose(file);
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* VALIDATION FUNCTIONS                                                       */
/*============================================================================*/

// Local CRC32C calculator (polynomial 0x82F63B78, reflected)
static uint32_t Parser_CalcCRC32C(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0x82F63B78u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

#ifndef ALGO_COUNT
// Fallback: if ALGO_COUNT is not defined, use ALGO_UNKNOWN as the sentinel upper bound
#define ALGO_COUNT (ALGO_UNKNOWN)
#endif

#ifndef COMP_HEADER_SIZE_V3
#define COMP_HEADER_SIZE_V3 64
#endif

#ifndef COMP_HEADER_SIZE_V4
#define COMP_HEADER_SIZE_V4 64
#endif

bool Parser_ValidateHeader(const CompHeader* header) {
    if (!header) {
        return false;
    }

    // Validate magic (COMP or Hardcore alt-magic)
    if (header->magic != 0x504D4F43 && header->magic != 0x01EFAD) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid magic: 0x%08X", header->magic);
        return false;
    }

    // Version must be strictly 3 or 4
    if (header->version != 3 && header->version != 4) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid version: %d", header->version);
        return false;
    }

    // Sizes must be non-zero and meet new constraints
    if (header->original_size == 0 || header->compressed_size == 0) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid sizes: orig=%lu, comp=%lu",
                  header->original_size, header->compressed_size);
        return false;
    }

    // Reject files where original_size exceeds 1 GiB
    const uint64_t MAX_ORIG = 1073741824ULL; // 1 GiB
    if (header->original_size > MAX_ORIG) {
        Logger_Log(LOG_LEVEL_ERROR, "Original size too large: %lu", header->original_size);
        return false;
    }

    // Reject compressed_size > original_size * 1.05
    // Use integer arithmetic to avoid floating point rounding issues
    // compressed_size * 100 > original_size * 105
    if (header->compressed_size * 100ULL > header->original_size * 105ULL) {
        Logger_Log(LOG_LEVEL_ERROR, "Compressed size too large relative to original: comp=%lu, orig=%lu",
                   header->compressed_size, header->original_size);
        return false;
    }

    // Algorithm must be less than ALGO_COUNT
    if (header->algorithm >= ALGO_COUNT) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid algorithm: %d", header->algorithm);
        return false;
    }

    // CRC32C header checksum validation (only if checksum field is present/non-zero)
    // V3 includes checksum; V4 often does not. Validate when meaningful.
    if (header->checksum != 0) {
        size_t header_size = (header->version == 3) ? COMP_HEADER_SIZE_V3 : COMP_HEADER_SIZE_V4;
        if (header_size > sizeof(CompHeader)) {
            header_size = sizeof(CompHeader);
        }

        // Compute CRC32C over the header bytes
        uint32_t computed = Parser_CalcCRC32C((const uint8_t*)header, header_size);
        if (computed != header->checksum) {
            Logger_Log(LOG_LEVEL_ERROR, "Header CRC32C mismatch: expected=0x%08X, computed=0x%08X",
                       header->checksum, computed);
            return false;
        }
    }

    // Extension can be empty; warn but don't fail
    if (strlen(header->original_extension) == 0) {
        Logger_Log(LOG_LEVEL_WARNING, "Empty original extension");
    }

    return true;
}

/*============================================================================*/
/* PRIVATE HELPER FUNCTIONS                                                   */
/*============================================================================*/

static bool ValidateHeaderIntegrity(const InternalCompHeader* header) {
    // Basic sanity checks
    if (header->original_size < header->compressed_size) {
        Logger_Log(LOG_LEVEL_WARNING, "Original size < compressed size: %lu < %lu",
                  header->original_size, header->compressed_size);
        // This could be valid for very small files, so just warn
    }
    
    if (header->compressed_size > header->original_size * 10) {
        Logger_Log(LOG_LEVEL_ERROR, "Compression ratio too extreme: %lu -> %lu",
                  header->original_size, header->compressed_size);
        return false;
    }
    
    return true;
}

static DecompStatus DetectCorruptionType(const uint8_t* data, size_t size) {
    if (!data || size < 8) {
        return DECOMP_STATUS_CORRUPTED_HEADER;
    }
    
    uint32_t magic = *(uint32_t*)data;
    magic = le32toh(magic);
    
    if (magic != 0x504D4F43) {
        return DECOMP_STATUS_INVALID_MAGIC;
    }
    
    uint8_t version = data[4];
    if (version != 3 && version != 4) {
        return DECOMP_STATUS_UNSUPPORTED_VERSION;
    }
    
    return DECOMP_STATUS_SUCCESS;
}

static void LogHeaderInfo(const InternalCompHeader* header) {
    Logger_Log(LOG_LEVEL_INFO, "COMP Header Information:");
    Logger_Log(LOG_LEVEL_INFO, "  Magic: 0x%08X", header->magic);
    Logger_Log(LOG_LEVEL_INFO, "  Version: %d", header->version);
    Logger_Log(LOG_LEVEL_INFO, "  Algorithm: %d", header->algorithm);
    Logger_Log(LOG_LEVEL_INFO, "  Original Size: %lu bytes", header->original_size);
    Logger_Log(LOG_LEVEL_INFO, "  Compressed Size: %lu bytes", header->compressed_size);
    Logger_Log(LOG_LEVEL_INFO, "  Checksum: 0x%08X", header->checksum);
    Logger_Log(LOG_LEVEL_INFO, "  Original Extension: %s", header->original_extension);
    Logger_Log(LOG_LEVEL_INFO, "  Timestamp: %ld", header->timestamp);
    
    if (header->version == 4) {
        Logger_Log(LOG_LEVEL_INFO, "  Block Count: %u", header->block_count);
        Logger_Log(LOG_LEVEL_INFO, "  Flags: 0x%08X", header->flags);
    }
}

/*============================================================================*/
/* MODULE CLEANUP                                                             */
/*============================================================================*/

void Parser_Cleanup(void) {
    Logger_Log(LOG_LEVEL_INFO, "Parser module cleaned up");
}

/*============================================================================*/
/* BYTE ORDER CONVERSION HELPERS                                              */
/*============================================================================*/

// Use the external byte order conversion functions