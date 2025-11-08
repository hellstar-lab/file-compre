/******************************************************************************
 * Advanced File Compressor - Production Grade Decompression Module
 * Module: Decompressor Header
 * Description: Multi-phase decompression architecture with full error recovery
 ******************************************************************************/

#ifndef DECOMPRESSOR_H
#define DECOMPRESSOR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
    #include <direct.h>
    #include <windows.h>
    #define mkdir _mkdir
#else
    #include <unistd.h>
    #include <dirent.h>
#endif

/*============================================================================*/
/* CONSTANTS AND LIMITS                                                       */
/*============================================================================*/

#define DECOMP_MAX_FILENAME     512
#define DECOMP_MAX_PATH         1024
#define DECOMP_BUFFER_SIZE      65536
#define DECOMP_MAX_BLOCKS       16384
#define DECOMP_MAX_DICTIONARY   (1 << 20)  /* 1MB max dictionary */
#define DECOMP_MAX_REASONABLE   (512L * 1024 * 1024)  /* 512MB safety limit */

/* Magic numbers for format detection */
#define COMP_MAGIC_V3           "COMP"
#define COMP_MAGIC_V4           "COMP"
#define HARDCORE_MAGIC          0xADEF01

/*============================================================================*/
/* ENUMERATIONS                                                               */
/*============================================================================*/

typedef enum {
    DECOMP_STATUS_SUCCESS = 0,
    DECOMP_STATUS_INVALID_MAGIC,
    DECOMP_STATUS_UNSUPPORTED_VERSION,
    DECOMP_STATUS_CORRUPTED_HEADER,
    DECOMP_STATUS_TRUNCATED_DATA,
    DECOMP_STATUS_INVALID_ALGORITHM,
    DECOMP_STATUS_DECOMPRESSION_ERROR,
    DECOMP_STATUS_INTEGRITY_FAILURE,
    DECOMP_STATUS_MEMORY_ERROR,
    DECOMP_STATUS_IO_ERROR,
    DECOMP_STATUS_INVALID_ARGUMENT
} DecompStatus;

typedef enum {
    ALGO_HUFFMAN = 0,
    ALGO_LZ77,
    ALGO_LZW,
    ALGO_RLE,
    ALGO_HARDCORE,
    ALGO_AUDIO_ADVANCED,
    ALGO_IMAGE_ADVANCED,
    ALGO_UNKNOWN = 255
} DecompAlgorithm;

typedef enum {
    FILE_TYPE_TEXT,
    FILE_TYPE_CSV,
    FILE_TYPE_JSON,
    FILE_TYPE_XML,
    FILE_TYPE_DOCX,
    FILE_TYPE_PDF,
    FILE_TYPE_AUDIO,
    // Specific image formats (24-bit focus)
    FILE_TYPE_BMP,
    FILE_TYPE_PNG,
    FILE_TYPE_TGA,
    FILE_TYPE_IMAGE,
    FILE_TYPE_BINARY,
    FILE_TYPE_UNKNOWN_TYPE
} DecompFileType;

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE
} LogLevel;

/*============================================================================*/
/* STRUCTURES                                                                 */
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
} CompHeader;

typedef struct {
    uint32_t block_id;
    uint64_t original_size;
    uint64_t compressed_size;
    uint8_t  algorithm;
    uint32_t checksum;
    uint64_t data_offset;
} BlockMetadata;

typedef struct {
    uint8_t* data;
    size_t   size;
    size_t   capacity;
} DecompBuffer;

typedef struct {
    char     filename[DECOMP_MAX_FILENAME];
    char     filepath[DECOMP_MAX_PATH];
    DecompFileType type;
    uint64_t original_size;
    uint64_t compressed_size;
    double   compression_ratio;
    uint32_t checksum;
    bool     integrity_verified;
} FileInfo;

typedef struct {
    uint64_t total_files_processed;
    uint64_t total_files_successful;
    uint64_t total_files_failed;
    uint64_t total_bytes_original;
    uint64_t total_bytes_compressed;
    uint64_t total_bytes_decompressed;
    double   average_compression_ratio;
    time_t   start_time;
    time_t   end_time;
    double   processing_time_seconds;
} ProcessingStats;

typedef struct {
    LogLevel level;
    char     log_file[256];
    bool     enable_console_output;
    bool     enable_file_output;
    char     log_buffer[DECOMP_BUFFER_SIZE];
} Logger;

typedef struct {
    bool     auto_repair_enabled;
    bool     strict_validation;
    bool     generate_reports;
    bool     verify_integrity;
    bool     debug_mode;
    LogLevel log_level;
    char     output_directory[DECOMP_MAX_PATH];
    char     report_directory[DECOMP_MAX_PATH];
} DecompConfig;

/*============================================================================*/
/* FUNCTION PROTOTYPES - PARSER MODULE                                        */
/*============================================================================*/

/**
 * Initialize parser module
 */
int Parser_Init(void);

/**
 * Parse COMP container header from file
 */
DecompStatus Parser_ParseHeader(FILE* file, CompHeader* header);

/**
 * Parse block metadata for v4 containers
 */
DecompStatus Parser_ParseBlockMetadata(FILE* file, BlockMetadata* blocks, int* block_count);

/**
 * Detect file format and version
 */
DecompStatus Parser_DetectFormat(const char* filepath, uint32_t* magic, uint8_t* version);

/**
 * Validate header integrity
 */
bool Parser_ValidateHeader(const CompHeader* header);

/**
 * Cleanup parser resources
 */
void Parser_Cleanup(void);

/*============================================================================*/
/* FUNCTION PROTOTYPES - COMPRESSOR CORE MODULE                              */
/*============================================================================*/

/**
 * Initialize compressor core
 */
int CompressorCore_Init(void);

/**
 * Decompress single block using specified algorithm
 */
DecompStatus CompressorCore_DecompressBlock(const uint8_t* compressed, size_t compressed_size,
                                           uint8_t** decompressed, size_t* decompressed_size,
                                           DecompAlgorithm algorithm);

/**
 * Select appropriate decompression algorithm
 */
DecompAlgorithm CompressorCore_SelectAlgorithm(const CompHeader* header, DecompFileType file_type);

/**
 * Validate decompression result
 */
bool CompressorCore_ValidateDecompression(const uint8_t* original, size_t original_size,
                                         const uint8_t* decompressed, size_t decompressed_size);

/**
 * Cleanup compressor core resources
 */
void CompressorCore_Cleanup(void);

/*============================================================================*/
/* FUNCTION PROTOTYPES - DSA ENGINE MODULE                                    */
/*============================================================================*/

/**
 * Initialize DSA (Data Structure Algorithms) engine
 */
int DSAEngine_Init(void);

/**
 * Reconstruct Huffman tree from compressed data
 */
DecompStatus DSAEngine_ReconstructHuffman(const uint8_t* data, size_t size,
                                         void** huffman_tree, size_t* tree_size);

/**
 * Reconstruct LZ77 dictionary
 */
DecompStatus DSAEngine_ReconstructLZ77Dictionary(const uint8_t* data, size_t size,
                                                void** dictionary, size_t* dict_size);

/**
 * Reconstruct LZW code table
 */
DecompStatus DSAEngine_ReconstructLZWTable(const uint8_t* data, size_t size,
                                          void** code_table, size_t* table_size);

/**
 * Validate DSA structure integrity
 */
bool DSAEngine_ValidateStructure(const void* structure, size_t size, DecompAlgorithm algorithm);

/**
 * Cleanup DSA engine resources
 */
void DSAEngine_Cleanup(void);

/*============================================================================*/
/* FUNCTION PROTOTYPES - DSSYNC MODULE                                        */
/*============================================================================*/

/**
 * Initialize DSSYNC (Data Structure Synchronizer)
 */
int DSSync_Init(void);

/**
 * Synchronize decompression state across blocks
 */
DecompStatus DSSync_SynchronizeState(BlockMetadata* blocks, int block_count,
                                  void** sync_state, size_t* state_size);

/**
 * Handle cross-block dependencies
 */
DecompStatus DSSync_HandleDependencies(const BlockMetadata* blocks, int block_count,
                                       void* sync_state);

/**
 * Validate synchronization state
 */
bool DSSync_ValidateState(const void* sync_state, size_t state_size);

/**
 * Cleanup DSSYNC resources
 */
void DSSync_Cleanup(void);

/*============================================================================*/
/* FUNCTION PROTOTYPES - FILE I/O MODULE                                    */
/*============================================================================*/

/**
 * Initialize file I/O module
 */
int FileIO_Init(void);

/**
 * Read compressed file into buffer
 */
DecompStatus FileIO_ReadCompressedFile(const char* filepath, uint8_t** buffer, size_t* size);

/**
 * Write decompressed data to file
 */
DecompStatus FileIO_WriteDecompressedFile(const char* filepath, const uint8_t* data, size_t size);

/**
 * Get file information
 */
DecompStatus FileIO_GetFileInfo(const char* filepath, FileInfo* info);

/**
 * Create output directory structure
 */
DecompStatus FileIO_CreateOutputStructure(const char* output_dir);

/**
 * Cleanup file I/O resources
 */
void FileIO_Cleanup(void);

/*============================================================================*/
/* FUNCTION PROTOTYPES - REPORT GENERATOR MODULE                              */
/*============================================================================*/

/**
 * Initialize report generator
 */
int ReportGenerator_Init(void);

/**
 * Generate processing report
 */
DecompStatus ReportGenerator_GenerateReport(const ProcessingStats* stats,
                                           const char* output_path);

/**
 * Generate detailed file analysis report
 */
DecompStatus ReportGenerator_GenerateFileReport(const FileInfo* info,
                                               const char* output_path);

/**
 * Generate error report
 */
DecompStatus ReportGenerator_GenerateErrorReport(const char* filename,
                                              DecompStatus error, const char* output_path);

/**
 * Cleanup report generator resources
 */
void ReportGenerator_Cleanup(void);

/*============================================================================*/
/* FUNCTION PROTOTYPES - ERROR RECOVERY MODULE                              */
/*============================================================================*/

/**
 * Initialize error recovery system
 */
int ErrorRecovery_Init(void);

/**
 * Attempt to repair corrupted header
 */
DecompStatus ErrorRecovery_RepairHeader(CompHeader* header);

/**
 * Attempt to recover from bitstream errors
 */
DecompStatus ErrorRecovery_RepairBitstream(uint8_t* data, size_t size,
                                          DecompAlgorithm algorithm);

/**
 * Reconstruct missing metadata
 */
DecompStatus ErrorRecovery_ReconstructMetadata(BlockMetadata* blocks, int* block_count);

/**
 * Validate repair success
 */
bool ErrorRecovery_ValidateRepair(const uint8_t* data, size_t size,
                                 const CompHeader* header);

/**
 * Cleanup error recovery resources
 */
void ErrorRecovery_Cleanup(void);

/*============================================================================*/
/* FUNCTION PROTOTYPES - MAIN DECOMPRESSOR INTERFACE                          */
/*============================================================================*/

/**
 * Initialize decompression system
 */
int Decompressor_Init(const DecompConfig* config);

/**
 * Decompress single file
 */
DecompStatus Decompressor_DecompressFile(const char* input_path, const char* output_path);

/**
 * Batch decompress all .comp files in directory
 */
DecompStatus Decompressor_DecompressDirectory(const char* input_dir, const char* output_dir);

/**
 * Analyze file without decompression
 */
DecompStatus Decompressor_AnalyzeFile(const char* filepath, FileInfo* info);

/**
 * Get processing statistics
 */
DecompStatus Decompressor_GetStats(ProcessingStats* stats);

/**
 * Cleanup decompression system
 */
void Decompressor_Cleanup(void);

/*============================================================================*/
/* BYTE ORDER CONVERSION FUNCTIONS                                            */
/*============================================================================*/

/**
 * Convert 32-bit little-endian to host byte order
 */
uint32_t le32toh(uint32_t value);

/**
 * Convert 64-bit little-endian to host byte order
 */
uint64_t le64toh(uint64_t value);

/**
 * Convert little-endian 16-bit integer to host byte order
 */
uint16_t le16toh(uint16_t value);

/**
 * Convert big-endian 32-bit integer to host byte order
 */
uint32_t be32toh(uint32_t value);

/**
 * Convert big-endian 64-bit integer to host byte order
 */
uint64_t be64toh(uint64_t value);

/**
 * Convert big-endian 16-bit integer to host byte order
 */
uint16_t be16toh(uint16_t value);

/*============================================================================*/
/* BIT I/O FUNCTIONS                                                          */
/*============================================================================*/

typedef struct BitReader BitReader;

/**
 * Create BitReader from file
 */
BitReader* BitReader_Create(FILE* file);

/**
 * Create BitReader from buffer
 */
BitReader* BitReader_CreateFromBuffer(const uint8_t* data, size_t size);

/**
 * Read single bit
 */
int BitReader_ReadBit(BitReader* br);

/**
 * Read multiple bits
 */
uint32_t BitReader_ReadBits(BitReader* br, int num_bits);

/**
 * Read byte
 */
uint8_t BitReader_ReadByte(BitReader* br);

/**
 * Skip bits
 */
bool BitReader_SkipBits(BitReader* br, int num_bits);

/**
 * Align to byte boundary
 */
bool BitReader_AlignToByte(BitReader* br);

/**
 * Check if EOF reached
 */
bool BitReader_IsEOF(BitReader* br);

/**
 * Check for errors
 */
bool BitReader_HasError(BitReader* br);

/**
 * Get bits read count
 */
uint64_t BitReader_GetBitsRead(BitReader* br);

/**
 * Get bytes read count
 */
uint64_t BitReader_GetBytesRead(BitReader* br);

/**
 * Destroy BitReader
 */
void BitReader_Destroy(BitReader* br);

/**
 * Debug sync
 */
void BitReader_DebugSync(BitReader* br, const char* context);

/**
 * Seek to bit position
 */
bool BitReader_Seek(BitReader* br, uint64_t bit_position);

/*============================================================================*/
/* CRC32 FUNCTIONS                                                            */
/*============================================================================*/

typedef struct {
    uint32_t crc;
    bool initialized;
} CRC32Context;

/**
 * Calculate CRC32 checksum
 */
uint32_t CRC32_Calculate(const uint8_t* data, size_t size);

/**
 * Calculate CRC32 for file
 */
uint32_t CRC32_CalculateFile(const char* filepath);

/**
 * Verify CRC32 checksum
 */
bool CRC32_Verify(const uint8_t* data, size_t size, uint32_t expected_crc);

/**
 * Verify CRC32 for file
 */
bool CRC32_VerifyFile(const char* filepath, uint32_t expected_crc);

/**
 * Create CRC32 context for streaming
 */
CRC32Context* CRC32_CreateContext(void);

/**
 * Update CRC32 context
 */
void CRC32_UpdateContext(CRC32Context* ctx, const uint8_t* data, size_t size);

/**
 * Finalize CRC32 context
 */
uint32_t CRC32_FinalizeContext(CRC32Context* ctx);

/**
 * Destroy CRC32 context
 */
void CRC32_DestroyContext(CRC32Context* ctx);

/**
 * CRC32 self-test
 */
bool CRC32_SelfTest(void);

/*============================================================================*/
/* BATCH DECOMPRESSOR FUNCTIONS                                               */
/*============================================================================*/

/**
 * Process all .comp files in directory
 */
DecompStatus BatchDecompressor_ProcessDirectory(const char* input_dir, const char* output_dir);

/**
 * Process all files (alias for ProcessDirectory)
 */
DecompStatus BatchDecompressor_ProcessAllFiles(const char* input_dir, const char* output_dir);

/*============================================================================*/
/* FILE ANALYZER FUNCTIONS                                                    */
/*============================================================================*/

/**
 * Analyze all .comp files in directory
 */
int FileAnalyzer_AnalyzeDirectory(const char* directory, const char* report_path);

/**
 * Analyze single .comp file
 */
int FileAnalyzer_AnalyzeSingle(const char* filepath);

/*============================================================================*/
/* COMPREHENSIVE TESTER FUNCTIONS                                             */
/*============================================================================*/

/**
 * Run comprehensive tests on all .comp files
 */
int ComprehensiveTester_RunTests(const char* input_dir, const char* output_dir, 
                                const char* original_dir, const char* log_dir);

/*============================================================================*/
/* FILE REPAIR TOOL FUNCTIONS                                                 */
/*============================================================================*/

/**
 * Repair corrupted .comp files in directory
 */
int FileRepairTool_RepairDirectory(const char* directory);

/*============================================================================*/
/* FALLBACK DECOMPRESSOR FUNCTIONS                                            */
/*============================================================================*/

/**
 * Process single file with fallback strategies
 */
int FallbackDecompressor_ProcessFile(const char* input_file, const char* output_file);

/**
 * Process directory with fallback strategies
 */
int FallbackDecompressor_ProcessDirectory(const char* input_dir, const char* output_dir);

/*============================================================================*/
/* UTILITY FUNCTIONS                                                          */
/*============================================================================*/

/**
 * Calculate CRC32 checksum (legacy)
 */
uint32_t Utility_CalculateChecksum(const uint8_t* data, size_t size);

/**
 * Detect file type from content
 */
DecompFileType Utility_DetectFileType(const uint8_t* data, size_t size);

/**
 * Get algorithm name as string
 */
const char* Utility_GetAlgorithmName(DecompAlgorithm algorithm);

/**
 * Get status description
 */
const char* Utility_GetStatusDescription(DecompStatus status);

/**
 * Format file size for display
 */
void Utility_FormatFileSize(uint64_t size, char* buffer, size_t buffer_size);

/**
 * Generate timestamp string
 */
void Utility_GetTimestamp(char* buffer, size_t buffer_size);

/*============================================================================*/
/* LOGGING FUNCTIONS                                                          */
/*============================================================================*/

/**
 * Initialize logging system
 */
int Logger_Init(const Logger* logger);

/**
 * Log message with specified level
 */
void Logger_Log(LogLevel level, const char* format, ...);

/**
 * Log error with context
 */
void Logger_LogError(const char* function, DecompStatus status, const char* context);

/**
 * Log file processing start
 */
void Logger_LogFileStart(const char* filename);

/**
 * Log file processing completion
 */
void Logger_LogFileComplete(const char* filename, DecompStatus status, uint64_t original_size, uint64_t decompressed_size);

/**
 * Cleanup logging system
 */
void Logger_Cleanup(void);

#endif /* DECOMPRESSOR_H */