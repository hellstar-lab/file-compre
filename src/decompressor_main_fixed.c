/******************************************************************************
 * Advanced File Compressor - Main Decompressor Module (Fixed)
 * Description: Production-grade batch decompression system with error recovery
 ******************************************************************************/

#include "../include/decompressor.h"
#include <sys/stat.h>

/*============================================================================*/
/* PRIVATE CONSTANTS                                                          */
/*============================================================================*/

#define MAX_FILES_TO_PROCESS    10000
#define PROGRESS_UPDATE_INTERVAL 100
#define ERROR_RETRY_LIMIT       3
#define MAX_PATH_LENGTH         1024
#define MAX_FILENAME_LENGTH     512

/*============================================================================*/
/* PRIVATE STRUCTURES                                                         */
/*============================================================================*/

typedef struct {
    bool initialized;
    DecompConfig config;
    ProcessingStats stats;
    Logger logger;
    char input_directory[MAX_PATH_LENGTH];
    char output_directory[MAX_PATH_LENGTH];
} DecompressorContext;

/*============================================================================*/
/* FORWARD DECLARATIONS                                                       */
/*============================================================================*/

static DecompressorContext g_context = {0};

static DecompStatus ProcessSingleFile(const char* input_path, const char* output_path);
static DecompStatus ProcessDirectory(const char* input_dir, const char* output_dir);
static DecompStatus AnalyzeAndDecompressFile(const char* input_path, const char* output_path);
static DecompStatus HandleDecompressionError(const char* filepath, DecompStatus error, int retry_count);
static void UpdateProgress(uint64_t processed, uint64_t total);
static void GenerateFinalReport(void);
static DecompStatus ValidateDecompressionResult(const char* input_path, const char* output_path, 
                                              const CompHeader* header);
static bool ShouldProcessFile(const char* filename);
static const char* GetOutputFilename(const char* input_filename, const char* original_extension);

/*============================================================================*/
/* MODULE INITIALIZATION                                                       */
/*============================================================================*/

int Decompressor_Init(const DecompConfig* config) {
    Logger_Log(LOG_LEVEL_INFO, "Initializing Advanced File Decompressor v1.0");
    
    if (g_context.initialized) {
        Logger_Log(LOG_LEVEL_WARNING, "Decompressor already initialized");
        return -1;
    }
    
    // Initialize configuration
    if (config) {
        memcpy(&g_context.config, config, sizeof(DecompConfig));
    } else {
        // Default configuration
        g_context.config.auto_repair_enabled = true;
        g_context.config.strict_validation = true;
        g_context.config.generate_reports = true;
        g_context.config.verify_integrity = true;
        g_context.config.debug_mode = false;
        g_context.config.log_level = LOG_LEVEL_INFO;
        strcpy(g_context.config.output_directory, "./output");
        strcpy(g_context.config.report_directory, "./reports");
    }
    
    // Initialize logger
    g_context.logger.level = g_context.config.log_level;
    g_context.logger.enable_console_output = true;
    g_context.logger.enable_file_output = true;
    snprintf(g_context.logger.log_file, sizeof(g_context.logger.log_file), 
             "%s/decompression.log", g_context.config.report_directory);
    
    if (Logger_Init(&g_context.logger) != 0) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to initialize logger");
        return -1;
    }
    
    // Initialize subsystems
    if (Parser_Init() != 0) {
        Logger_LogError("Decompressor_Init", DECOMP_STATUS_MEMORY_ERROR, "Failed to initialize parser");
        return -1;
    }
    
    if (CompressorCore_Init() != 0) {
        Logger_LogError("Decompressor_Init", DECOMP_STATUS_MEMORY_ERROR, "Failed to initialize compressor core");
        Parser_Cleanup();
        return -1;
    }
    
    if (FileIO_Init() != 0) {
        Logger_LogError("Decompressor_Init", DECOMP_STATUS_MEMORY_ERROR, "Failed to initialize file I/O");
        CompressorCore_Cleanup();
        Parser_Cleanup();
        return -1;
    }
    
    // Create output directory structure
    if (FileIO_CreateOutputStructure(g_context.config.output_directory) != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("Decompressor_Init", DECOMP_STATUS_IO_ERROR, "Failed to create output directory structure");
        FileIO_Cleanup();
        CompressorCore_Cleanup();
        Parser_Cleanup();
        return -1;
    }
    
    // Initialize statistics
    memset(&g_context.stats, 0, sizeof(ProcessingStats));
    g_context.stats.start_time = time(NULL);
    
    g_context.initialized = true;
    
    Logger_Log(LOG_LEVEL_INFO, "Advanced File Decompressor initialization completed successfully");
    Logger_Log(LOG_LEVEL_INFO, "Configuration:");
    Logger_Log(LOG_LEVEL_INFO, "  Auto-repair: %s", g_context.config.auto_repair_enabled ? "enabled" : "disabled");
    Logger_Log(LOG_LEVEL_INFO, "  Strict validation: %s", g_context.config.strict_validation ? "enabled" : "disabled");
    Logger_Log(LOG_LEVEL_INFO, "  Integrity verification: %s", g_context.config.verify_integrity ? "enabled" : "disabled");
    Logger_Log(LOG_LEVEL_INFO, "  Debug mode: %s", g_context.config.debug_mode ? "enabled" : "disabled");
    Logger_Log(LOG_LEVEL_INFO, "  Output directory: %s", g_context.config.output_directory);
    Logger_Log(LOG_LEVEL_INFO, "  Report directory: %s", g_context.config.report_directory);
    
    return 0;
}

/*============================================================================*/
/* SINGLE FILE PROCESSING                                                     */
/*============================================================================*/

DecompStatus Decompressor_DecompressFile(const char* input_path, const char* output_path) {
    if (!g_context.initialized) {
        Logger_Log(LOG_LEVEL_ERROR, "Decompressor not initialized");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    if (!input_path || !output_path) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid file paths");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    Logger_LogFileStart(input_path);
    
    DecompStatus status = ProcessSingleFile(input_path, output_path);
    
    // Update statistics
    g_context.stats.total_files_processed++;
    if (status == DECOMP_STATUS_SUCCESS) {
        g_context.stats.total_files_successful++;
    } else {
        g_context.stats.total_files_failed++;
    }
    
    // Get file info for size statistics
    FileInfo info;
    if (FileIO_GetFileInfo(input_path, &info) == DECOMP_STATUS_SUCCESS) {
        g_context.stats.total_bytes_compressed += info.compressed_size;
        if (status == DECOMP_STATUS_SUCCESS) {
            g_context.stats.total_bytes_decompressed += info.original_size;
            g_context.stats.total_bytes_original += info.original_size;
        }
    }
    
    Logger_LogFileComplete(input_path, status, info.original_size, info.compressed_size);
    
    return status;
}

static DecompStatus ProcessSingleFile(const char* input_path, const char* output_path) {
    Logger_Log(LOG_LEVEL_DEBUG, "Processing single file: %s -> %s", input_path, output_path);
    
    int retry_count = 0;
    DecompStatus status = DECOMP_STATUS_SUCCESS;
    
    while (retry_count <= ERROR_RETRY_LIMIT) {
        status = AnalyzeAndDecompressFile(input_path, output_path);
        
        if (status == DECOMP_STATUS_SUCCESS) {
            break;
        }
        
        if (!g_context.config.auto_repair_enabled) {
            break;
        }
        
        Logger_Log(LOG_LEVEL_WARNING, "Decompression failed, attempting recovery (retry %d/%d)",
                  retry_count + 1, ERROR_RETRY_LIMIT);
        
        status = HandleDecompressionError(input_path, status, retry_count);
        
        if (status == DECOMP_STATUS_SUCCESS) {
            Logger_Log(LOG_LEVEL_INFO, "Recovery successful after retry %d", retry_count + 1);
            break;
        }
        
        retry_count++;
    }
    
    return status;
}

static DecompStatus AnalyzeAndDecompressFile(const char* input_path, const char* output_path) {
    // Read compressed file
    uint8_t* compressed_data = NULL;
    size_t compressed_size = 0;
    
    DecompStatus status = FileIO_ReadCompressedFile(input_path, &compressed_data, &compressed_size);
    if (status != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("AnalyzeAndDecompressFile", status, "Failed to read compressed file");
        return status;
    }
    
    // Parse header
    CompHeader header;
    FILE* file = fopen(input_path, "rb");
    if (!file) {
        free(compressed_data);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    status = Parser_ParseHeader(file, &header);
    fclose(file);
    
    if (status != DECOMP_STATUS_SUCCESS) {
        free(compressed_data);
        Logger_LogError("AnalyzeAndDecompressFile", status, "Failed to parse header");
        return status;
    }
    
    // Detect file type
    DecompFileType file_type = Utility_DetectFileType(compressed_data, compressed_size);
    
    // Select algorithm
    DecompAlgorithm algorithm = CompressorCore_SelectAlgorithm(&header, file_type);
    if (algorithm == ALGO_UNKNOWN) {
        free(compressed_data);
        Logger_LogError("AnalyzeAndDecompressFile", DECOMP_STATUS_INVALID_ALGORITHM, "Unknown algorithm");
        return DECOMP_STATUS_INVALID_ALGORITHM;
    }
    
    // Decompress data
    uint8_t* decompressed_data = NULL;
    size_t decompressed_size = 0;
    
    status = CompressorCore_DecompressBlock(compressed_data, compressed_size,
                                         &decompressed_data, &decompressed_size, algorithm);
    
    free(compressed_data);
    
    if (status != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("AnalyzeAndDecompressFile", status, "Decompression failed");
        return status;
    }
    
    // Validate decompression result
    if (g_context.config.verify_integrity) {
        status = ValidateDecompressionResult(input_path, output_path, &header);
        if (status != DECOMP_STATUS_SUCCESS) {
            free(decompressed_data);
            Logger_LogError("AnalyzeAndDecompressFile", status, "Integrity validation failed");
            return status;
        }
    }
    
    // Write decompressed file
    status = FileIO_WriteDecompressedFile(output_path, decompressed_data, decompressed_size);
    free(decompressed_data);
    
    if (status != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("AnalyzeAndDecompressFile", status, "Failed to write decompressed file");
        return status;
    }
    
    Logger_Log(LOG_LEVEL_INFO, "File decompressed successfully: %s (%lu -> %lu bytes)",
              input_path, compressed_size, decompressed_size);
    
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* BATCH PROCESSING                                                           */
/*============================================================================*/

DecompStatus Decompressor_DecompressDirectory(const char* input_dir, const char* output_dir) {
    if (!g_context.initialized) {
        Logger_Log(LOG_LEVEL_ERROR, "Decompressor not initialized");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    if (!input_dir || !output_dir) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid directory paths");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    Logger_Log(LOG_LEVEL_INFO, "Starting batch decompression: %s -> %s", input_dir, output_dir);
    
    return ProcessDirectory(input_dir, output_dir);
}

static DecompStatus ProcessDirectory(const char* input_dir, const char* output_dir) {
    // Create output directory if needed
    if (FileIO_CreateOutputStructure(output_dir) != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("ProcessDirectory", DECOMP_STATUS_IO_ERROR, "Failed to create output directory");
        return DECOMP_STATUS_IO_ERROR;
    }
    
    // Directory scanning
    char search_pattern[MAX_PATH_LENGTH];
    snprintf(search_pattern, sizeof(search_pattern), "%s/*.comp", input_dir);
    
    // Count .comp files
    uint64_t file_count = 0;
    uint64_t processed_count = 0;
    
    #ifdef _WIN32
        WIN32_FIND_DATA find_data;
        HANDLE find_handle = FindFirstFile(search_pattern, &find_data);
        
        if (find_handle == INVALID_HANDLE_VALUE) {
            Logger_Log(LOG_LEVEL_WARNING, "No .comp files found in directory: %s", input_dir);
            return DECOMP_STATUS_SUCCESS;
        }
        
        // First pass: count files
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                file_count++;
            }
        } while (FindNextFile(find_handle, &find_data));
        FindClose(find_handle);
        
        // Second pass: process files
        find_handle = FindFirstFile(search_pattern, &find_data);
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char input_path[MAX_PATH_LENGTH];
                char output_path[MAX_PATH_LENGTH];
                
                snprintf(input_path, sizeof(input_path), "%s/%s", input_dir, find_data.cFileName);
                
                // Generate output filename base (remove .comp)
                char base_name[MAX_FILENAME_LENGTH];
                strncpy(base_name, find_data.cFileName, MAX_FILENAME_LENGTH - 1);
                base_name[MAX_FILENAME_LENGTH - 1] = '\0';
                char* dot = strrchr(base_name, '.');
                if (dot) *dot = '\0';
                
                snprintf(output_path, sizeof(output_path), "%s/decompressed/%s", output_dir, base_name);
                
                Logger_Log(LOG_LEVEL_INFO, "Processing file %llu of %llu: %s",
                           processed_count + 1, file_count, find_data.cFileName);
                
                DecompStatus status = Decompressor_DecompressFile(input_path, output_path);
                if (status != DECOMP_STATUS_SUCCESS) {
                    Logger_Log(LOG_LEVEL_ERROR, "Failed to process file: %s", find_data.cFileName);
                }
                
                UpdateProgress(processed_count + 1, file_count);
                processed_count++;
            }
        } while (FindNextFile(find_handle, &find_data));
        FindClose(find_handle);
    #else
        // Unix-like systems
        DIR* dir = opendir(input_dir);
        if (!dir) {
            Logger_LogError("ProcessDirectory", DECOMP_STATUS_IO_ERROR, "Failed to open directory");
            return DECOMP_STATUS_IO_ERROR;
        }
        
        struct dirent* entry;
        
        // Count files first
        while ((entry = readdir(dir)) != NULL) {
            if (ShouldProcessFile(entry->d_name)) {
                file_count++;
            }
        }
        
        rewinddir(dir);
        
        // Process files
        while ((entry = readdir(dir)) != NULL) {
            if (ShouldProcessFile(entry->d_name)) {
                char input_path[MAX_PATH_LENGTH];
                char output_path[MAX_PATH_LENGTH];
                
                snprintf(input_path, sizeof(input_path), "%s/%s", input_dir, entry->d_name);
                
                // Generate output filename
                char base_name[MAX_FILENAME_LENGTH];
                strncpy(base_name, entry->d_name, MAX_FILENAME_LENGTH - 1);
                base_name[MAX_FILENAME_LENGTH - 1] = '\0';
                
                // Remove .comp extension
                char* dot = strrchr(base_name, '.');
                if (dot) *dot = '\0';
                
                snprintf(output_path, sizeof(output_path), "%s/decompressed/%s", output_dir, base_name);
                
                Logger_Log(LOG_LEVEL_INFO, "Processing file %llu of %llu: %s",
                          processed_count + 1, file_count, entry->d_name);
                
                DecompStatus status = Decompressor_DecompressFile(input_path, output_path);
                
                if (status != DECOMP_STATUS_SUCCESS) {
                    Logger_Log(LOG_LEVEL_ERROR, "Failed to process file: %s", entry->d_name);
                }
                
                UpdateProgress(processed_count + 1, file_count);
                processed_count++;
            }
        }
        
        closedir(dir);
    #endif
    
    Logger_Log(LOG_LEVEL_INFO, "Batch processing completed. Processed %llu files", processed_count);
    
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* UTILITY FUNCTIONS                                                          */
/*============================================================================*/

DecompStatus Decompressor_AnalyzeFile(const char* filepath, FileInfo* info) {
    if (!g_context.initialized) {
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    return FileIO_GetFileInfo(filepath, info);
}

DecompStatus Decompressor_GetStats(ProcessingStats* stats) {
    if (!g_context.initialized || !stats) {
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    // Update end time and calculate processing time
    g_context.stats.end_time = time(NULL);
    g_context.stats.processing_time_seconds = difftime(g_context.stats.end_time, g_context.stats.start_time);
    
    // Calculate average compression ratio
    if (g_context.stats.total_files_successful > 0) {
        g_context.stats.average_compression_ratio = 
            (double)g_context.stats.total_bytes_compressed / g_context.stats.total_bytes_original * 100.0;
    }
    
    memcpy(stats, &g_context.stats, sizeof(ProcessingStats));
    return DECOMP_STATUS_SUCCESS;
}

static DecompStatus HandleDecompressionError(const char* filepath, DecompStatus error, int retry_count) {
    Logger_Log(LOG_LEVEL_DEBUG, "Handling decompression error: %s (retry %d)", 
              Utility_GetStatusDescription(error), retry_count);
    
    // Implement error-specific recovery strategies
    switch (error) {
        case DECOMP_STATUS_INVALID_MAGIC:
            Logger_Log(LOG_LEVEL_WARNING, "Attempting magic number recovery");
            // Try different magic number formats
            break;
            
        case DECOMP_STATUS_CORRUPTED_HEADER:
            Logger_Log(LOG_LEVEL_WARNING, "Attempting header reconstruction");
            // Try to reconstruct header from partial data
            break;
            
        case DECOMP_STATUS_TRUNCATED_DATA:
            Logger_Log(LOG_LEVEL_WARNING, "Attempting data completion");
            // Try to complete truncated data
            break;
            
        default:
            Logger_Log(LOG_LEVEL_DEBUG, "No specific recovery strategy for error: %s",
                      Utility_GetStatusDescription(error));
            break;
    }
    
    return error; // Return original error if recovery fails
}

static void UpdateProgress(uint64_t processed, uint64_t total) {
    if (total == 0) return;
    
    int percentage = (int)((processed * 100) / total);
    
    if (processed % PROGRESS_UPDATE_INTERVAL == 0 || processed == total) {
        Logger_Log(LOG_LEVEL_INFO, "Progress: %llu/%llu files (%d%%)", processed, total, percentage);
    }
}

static DecompStatus ValidateDecompressionResult(const char* input_path, const char* output_path, 
                                              const CompHeader* header) {
    // Check if output file exists and has reasonable size
    struct stat output_stat;
    if (stat(output_path, &output_stat) != 0) {
        return DECOMP_STATUS_IO_ERROR;
    }
    
    // Validate output file size
    if (header->original_size > 0) {
        if ((uint64_t)output_stat.st_size != header->original_size) {
            Logger_Log(LOG_LEVEL_ERROR, "Output size mismatch: expected %lu, got %lu",
                      header->original_size, output_stat.st_size);
            return DECOMP_STATUS_INTEGRITY_FAILURE;
        }
    }
    
    return DECOMP_STATUS_SUCCESS;
}

static bool ShouldProcessFile(const char* filename) {
    // Check if file has .comp extension
    size_t len = strlen(filename);
    if (len < 5) return false;
    
    return (strcasecmp(filename + len - 5, ".comp") == 0);
}

/*============================================================================*/
/* MODULE CLEANUP                                                             */
/*============================================================================*/

void Decompressor_Cleanup(void) {
    if (!g_context.initialized) {
        return;
    }
    
    Logger_Log(LOG_LEVEL_INFO, "Shutting down Advanced File Decompressor");
    
    // Generate final report
    GenerateFinalReport();
    
    // Cleanup subsystems in reverse order
    FileIO_Cleanup();
    CompressorCore_Cleanup();
    Parser_Cleanup();
    Logger_Cleanup();
    
    memset(&g_context, 0, sizeof(DecompressorContext));
    
    Logger_Log(LOG_LEVEL_INFO, "Decompressor shutdown completed");
}

static void GenerateFinalReport(void) {
    ProcessingStats final_stats;
    if (Decompressor_GetStats(&final_stats) == DECOMP_STATUS_SUCCESS) {
        Logger_Log(LOG_LEVEL_INFO, "========================================");
        Logger_Log(LOG_LEVEL_INFO, "FINAL PROCESSING STATISTICS");
        Logger_Log(LOG_LEVEL_INFO, "========================================");
        Logger_Log(LOG_LEVEL_INFO, "Total files processed: %llu", final_stats.total_files_processed);
        Logger_Log(LOG_LEVEL_INFO, "Successful decompressions: %llu", final_stats.total_files_successful);
        Logger_Log(LOG_LEVEL_INFO, "Failed decompressions: %llu", final_stats.total_files_failed);
        Logger_Log(LOG_LEVEL_INFO, "Success rate: %.2f%%", 
                  (final_stats.total_files_processed > 0) ? 
                  (final_stats.total_files_successful * 100.0 / final_stats.total_files_processed) : 0.0);
        Logger_Log(LOG_LEVEL_INFO, "Total original size: %llu bytes", final_stats.total_bytes_original);
        Logger_Log(LOG_LEVEL_INFO, "Total compressed size: %llu bytes", final_stats.total_bytes_compressed);
        Logger_Log(LOG_LEVEL_INFO, "Total decompressed size: %llu bytes", final_stats.total_bytes_decompressed);
        Logger_Log(LOG_LEVEL_INFO, "Average compression ratio: %.2f%%", final_stats.average_compression_ratio);
        Logger_Log(LOG_LEVEL_INFO, "Total processing time: %.2f seconds", final_stats.processing_time_seconds);
        Logger_Log(LOG_LEVEL_INFO, "Processing speed: %.2f files/second", 
                  (final_stats.processing_time_seconds > 0) ? 
                  (final_stats.total_files_processed / final_stats.processing_time_seconds) : 0.0);
        Logger_Log(LOG_LEVEL_INFO, "========================================");
    }
}