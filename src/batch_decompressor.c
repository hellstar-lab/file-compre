/******************************************************************************
 * Advanced File Compressor - Batch Decompressor Module
 * Description: Production-grade batch decompression with auto-repair and logging
 ******************************************************************************/

#include "../include/decompressor.h"
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// Portable strcasecmp shim
#if defined(_WIN32) && !defined(__MINGW32__)
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#else
#include <strings.h>
#endif

/*============================================================================*/
/* CONSTANTS                                                                  */
/*============================================================================*/

#define BATCH_MAX_FILES         10000
#define BATCH_PROGRESS_INTERVAL 10
#define BATCH_RETRY_LIMIT       3
#define BATCH_BUFFER_SIZE       65536

/*============================================================================*/
/* STRUCTURES                                                                 */
/*============================================================================*/

typedef struct {
    char input_path[DECOMP_MAX_PATH];
    char output_path[DECOMP_MAX_PATH];
    char original_name[DECOMP_MAX_FILENAME];
    uint64_t original_size;
    uint64_t compressed_size;
    uint64_t decompressed_size;
    DecompStatus status;
    int retry_count;
    double processing_time;
    uint32_t original_checksum;
    uint32_t decompressed_checksum;
    bool integrity_verified;
} BatchFileInfo;

typedef struct {
    BatchFileInfo* files;
    int file_count;
    int files_processed;
    int files_successful;
    int files_failed;
    int files_repaired;
    uint64_t total_original_bytes;
    uint64_t total_compressed_bytes;
    uint64_t total_decompressed_bytes;
    double total_processing_time;
    time_t start_time;
    time_t end_time;
    char input_directory[DECOMP_MAX_PATH];
    char output_directory[DECOMP_MAX_PATH];
    char report_path[DECOMP_MAX_PATH];
} BatchContext;

/*============================================================================*/
/* GLOBAL VARIABLES                                                           */
/*============================================================================*/

static BatchContext g_batch_context = {0};

/*============================================================================*/
/* PRIVATE FUNCTIONS                                                          */
/*============================================================================*/

static bool IsCompFile(const char* filename) {
    if (!filename) return false;
    
    size_t len = strlen(filename);
    if (len < 5) return false;
    
    return (strcasecmp(filename + len - 5, ".comp") == 0);
}

static void GetOutputFilename(const char* input_filename, char* output_filename, size_t output_size) {
    if (!input_filename || !output_filename) return;
    
    // Remove .comp extension
    strncpy(output_filename, input_filename, output_size - 1);
    output_filename[output_size - 1] = '\0';
    
    size_t len = strlen(output_filename);
    if (len > 5 && strcasecmp(output_filename + len - 5, ".comp") == 0) {
        output_filename[len - 5] = '\0';
    }
}

static DecompStatus ProcessSingleBatchFile(BatchFileInfo* file_info) {
    if (!file_info) return DECOMP_STATUS_INVALID_ARGUMENT;
    
    Logger_Log(LOG_LEVEL_INFO, "Processing: %s", file_info->input_path);
    
    clock_t start_time = clock();
    
    // Read compressed file
    uint8_t* compressed_data = NULL;
    size_t compressed_size = 0;
    
    DecompStatus status = FileIO_ReadCompressedFile(file_info->input_path, &compressed_data, &compressed_size);
    if (status != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("ProcessSingleBatchFile", status, "Failed to read compressed file");
        return status;
    }
    
    file_info->compressed_size = compressed_size;
    
    // Parse header
    CompHeader header;
    FILE* file = fopen(file_info->input_path, "rb");
    if (!file) {
        free(compressed_data);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    status = Parser_ParseHeader(file, &header);
    fclose(file);
    
    if (status != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("ProcessSingleBatchFile", status, "Header parsing failed");
        free(compressed_data);
        return status;
    }
    
    file_info->original_size = header.original_size;
    file_info->original_checksum = header.checksum;
    
    // Select algorithm
    DecompFileType file_type = Utility_DetectFileType(compressed_data, compressed_size);
    DecompAlgorithm algorithm = CompressorCore_SelectAlgorithm(&header, file_type);
    
    // Decompress
    uint8_t* decompressed_data = NULL;
    size_t decompressed_size = 0;
    
    status = CompressorCore_DecompressBlock(compressed_data, compressed_size,
                                          &decompressed_data, &decompressed_size, algorithm);
    
    free(compressed_data);
    
    if (status != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("ProcessSingleBatchFile", status, "Decompression failed");
        return status;
    }
    
    file_info->decompressed_size = decompressed_size;
    
    // Verify integrity
    if (header.checksum != 0) {
        file_info->decompressed_checksum = CRC32_Calculate(decompressed_data, decompressed_size);
        file_info->integrity_verified = CRC32_Verify(decompressed_data, decompressed_size, header.checksum);
        
        if (!file_info->integrity_verified) {
            Logger_Log(LOG_LEVEL_WARNING, "Checksum mismatch for %s", file_info->input_path);
        }
    }
    
    // Write decompressed file
    status = FileIO_WriteDecompressedFile(file_info->output_path, decompressed_data, decompressed_size);
    
    free(decompressed_data);
    
    if (status != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("ProcessSingleBatchFile", status, "Failed to write decompressed file");
        return status;
    }
    
    // Calculate processing time
    clock_t end_time = clock();
    file_info->processing_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    Logger_Log(LOG_LEVEL_INFO, "Successfully processed: %s (%.2f seconds)", 
               file_info->input_path, file_info->processing_time);
    
    return DECOMP_STATUS_SUCCESS;
}

static DecompStatus ScanDirectory(const char* input_dir) {
    if (!input_dir) return DECOMP_STATUS_INVALID_ARGUMENT;
    
    g_batch_context.file_count = 0;
    g_batch_context.files = (BatchFileInfo*)malloc(BATCH_MAX_FILES * sizeof(BatchFileInfo));
    if (!g_batch_context.files) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to allocate memory for file list");
        return DECOMP_STATUS_MEMORY_ERROR;
    }
    
    Logger_Log(LOG_LEVEL_INFO, "Scanning directory: %s", input_dir);
    
#ifdef _WIN32
    char search_pattern[DECOMP_MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.comp", input_dir);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFile(search_pattern, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        Logger_Log(LOG_LEVEL_WARNING, "No .comp files found in directory: %s", input_dir);
        return DECOMP_STATUS_SUCCESS;
    }
    
    do {
        if (g_batch_context.file_count >= BATCH_MAX_FILES) {
            Logger_Log(LOG_LEVEL_WARNING, "Maximum file limit reached: %d", BATCH_MAX_FILES);
            break;
        }
        
        BatchFileInfo* file_info = &g_batch_context.files[g_batch_context.file_count];
        memset(file_info, 0, sizeof(BatchFileInfo));
        
        // Build input path
        snprintf(file_info->input_path, sizeof(file_info->input_path), 
                 "%s\\%s", input_dir, find_data.cFileName);
        
        // Build output path
        char output_filename[DECOMP_MAX_FILENAME];
        GetOutputFilename(find_data.cFileName, output_filename, sizeof(output_filename));
        snprintf(file_info->output_path, sizeof(file_info->output_path),
                 "%s\\%s", g_batch_context.output_directory, output_filename);
        
        strncpy(file_info->original_name, find_data.cFileName, sizeof(file_info->original_name) - 1);
        
        g_batch_context.file_count++;
        
    } while (FindNextFile(find_handle, &find_data));
    
    FindClose(find_handle);
    
#else
    DIR* dir = opendir(input_dir);
    if (!dir) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to open directory: %s", input_dir);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && g_batch_context.file_count < BATCH_MAX_FILES) {
        if (!IsCompFile(entry->d_name)) continue;
        
        BatchFileInfo* file_info = &g_batch_context.files[g_batch_context.file_count];
        memset(file_info, 0, sizeof(BatchFileInfo));
        
        // Build input path
        snprintf(file_info->input_path, sizeof(file_info->input_path), 
                 "%s/%s", input_dir, entry->d_name);
        
        // Build output path
        char output_filename[DECOMP_MAX_FILENAME];
        GetOutputFilename(entry->d_name, output_filename, sizeof(output_filename));
        snprintf(file_info->output_path, sizeof(file_info->output_path),
                 "%s/%s", g_batch_context.output_directory, output_filename);
        
        strncpy(file_info->original_name, entry->d_name, sizeof(file_info->original_name) - 1);
        
        g_batch_context.file_count++;
    }
    
    closedir(dir);
#endif
    
    Logger_Log(LOG_LEVEL_INFO, "Found %d .comp files", g_batch_context.file_count);
    return DECOMP_STATUS_SUCCESS;
}

static void UpdateBatchProgress(int current, int total) {
    if (total == 0) return;
    
    double percentage = ((double)current / total) * 100.0;
    
    if (current % BATCH_PROGRESS_INTERVAL == 0 || current == total) {
        Logger_Log(LOG_LEVEL_INFO, "Progress: %d/%d (%.1f%%) - Success: %d, Failed: %d", 
                   current, total, percentage, 
                   g_batch_context.files_successful, g_batch_context.files_failed);
    }
}

static void GenerateBatchReport(void) {
    FILE* report = fopen(g_batch_context.report_path, "w");
    if (!report) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to create batch report: %s", g_batch_context.report_path);
        return;
    }
    
    fprintf(report, "Advanced File Compressor - Batch Decompression Report\n");
    fprintf(report, "=====================================================\n\n");
    
    char start_time_str[64], end_time_str[64];
    strftime(start_time_str, sizeof(start_time_str), "%Y-%m-%d %H:%M:%S", localtime(&g_batch_context.start_time));
    strftime(end_time_str, sizeof(end_time_str), "%Y-%m-%d %H:%M:%S", localtime(&g_batch_context.end_time));
    
    fprintf(report, "Start Time: %s\n", start_time_str);
    fprintf(report, "End Time: %s\n", end_time_str);
    fprintf(report, "Total Processing Time: %.2f seconds\n\n", g_batch_context.total_processing_time);
    
    fprintf(report, "Summary:\n");
    fprintf(report, "--------\n");
    fprintf(report, "Total Files: %d\n", g_batch_context.file_count);
    fprintf(report, "Successful: %d\n", g_batch_context.files_successful);
    fprintf(report, "Failed: %d\n", g_batch_context.files_failed);
    fprintf(report, "Repaired: %d\n", g_batch_context.files_repaired);
    fprintf(report, "Success Rate: %.1f%%\n\n", 
            g_batch_context.file_count > 0 ? 
            ((double)g_batch_context.files_successful / g_batch_context.file_count) * 100.0 : 0.0);
    
    fprintf(report, "Data Summary:\n");
    fprintf(report, "-------------\n");
    fprintf(report, "Total Original Bytes: %llu\n", g_batch_context.total_original_bytes);
    fprintf(report, "Total Compressed Bytes: %llu\n", g_batch_context.total_compressed_bytes);
    fprintf(report, "Total Decompressed Bytes: %llu\n", g_batch_context.total_decompressed_bytes);
    
    if (g_batch_context.total_original_bytes > 0) {
        double compression_ratio = ((double)g_batch_context.total_compressed_bytes / g_batch_context.total_original_bytes) * 100.0;
        fprintf(report, "Average Compression Ratio: %.1f%%\n", compression_ratio);
    }
    
    fprintf(report, "\nDetailed Results:\n");
    fprintf(report, "=================\n");
    fprintf(report, "%-40s %-10s %-12s %-12s %-12s %-8s %-10s\n", 
            "Filename", "Status", "Original", "Compressed", "Decompressed", "Time(s)", "Integrity");
    fprintf(report, "%s\n", "--------------------------------------------------------------------------------------------------------");
    
    for (int i = 0; i < g_batch_context.file_count; i++) {
        BatchFileInfo* file = &g_batch_context.files[i];
        const char* status_str = Utility_GetStatusDescription(file->status);
        const char* integrity_str = file->integrity_verified ? "PASS" : "FAIL";
        
        fprintf(report, "%-40s %-10s %-12llu %-12llu %-12llu %-8.2f %-10s\n",
                file->original_name, status_str, file->original_size, 
                file->compressed_size, file->decompressed_size, 
                file->processing_time, integrity_str);
    }
    
    fclose(report);
    Logger_Log(LOG_LEVEL_INFO, "Batch report generated: %s", g_batch_context.report_path);
}

/*============================================================================*/
/* PUBLIC FUNCTIONS                                                           */
/*============================================================================*/

DecompStatus BatchDecompressor_ProcessDirectory(const char* input_dir, const char* output_dir) {
    if (!input_dir || !output_dir) {
        Logger_Log(LOG_LEVEL_ERROR, "BatchDecompressor_ProcessDirectory: Invalid parameters");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    Logger_Log(LOG_LEVEL_INFO, "Starting batch decompression");
    Logger_Log(LOG_LEVEL_INFO, "Input directory: %s", input_dir);
    Logger_Log(LOG_LEVEL_INFO, "Output directory: %s", output_dir);
    
    // Initialize context
    memset(&g_batch_context, 0, sizeof(BatchContext));
    strncpy(g_batch_context.input_directory, input_dir, sizeof(g_batch_context.input_directory) - 1);
    strncpy(g_batch_context.output_directory, output_dir, sizeof(g_batch_context.output_directory) - 1);
    snprintf(g_batch_context.report_path, sizeof(g_batch_context.report_path), 
             "%s/batch_decompression_report.txt", output_dir);
    
    g_batch_context.start_time = time(NULL);
    
    // Create output directory
    DecompStatus status = FileIO_CreateOutputStructure(output_dir);
    if (status != DECOMP_STATUS_SUCCESS) {
        Logger_LogError("BatchDecompressor_ProcessDirectory", status, "Failed to create output directory");
        return status;
    }
    
    // Scan for .comp files
    status = ScanDirectory(input_dir);
    if (status != DECOMP_STATUS_SUCCESS) {
        return status;
    }
    
    if (g_batch_context.file_count == 0) {
        Logger_Log(LOG_LEVEL_WARNING, "No .comp files found to process");
        return DECOMP_STATUS_SUCCESS;
    }
    
    // Process files
    clock_t batch_start = clock();
    
    for (int i = 0; i < g_batch_context.file_count; i++) {
        BatchFileInfo* file_info = &g_batch_context.files[i];
        
        for (int retry = 0; retry <= BATCH_RETRY_LIMIT; retry++) {
            file_info->retry_count = retry;
            file_info->status = ProcessSingleBatchFile(file_info);
            
            if (file_info->status == DECOMP_STATUS_SUCCESS) {
                g_batch_context.files_successful++;
                g_batch_context.total_original_bytes += file_info->original_size;
                g_batch_context.total_compressed_bytes += file_info->compressed_size;
                g_batch_context.total_decompressed_bytes += file_info->decompressed_size;
                
                if (retry > 0) {
                    g_batch_context.files_repaired++;
                    Logger_Log(LOG_LEVEL_INFO, "File repaired after %d retries: %s", retry, file_info->input_path);
                }
                break;
            } else if (retry < BATCH_RETRY_LIMIT) {
                Logger_Log(LOG_LEVEL_WARNING, "Retry %d for file: %s", retry + 1, file_info->input_path);
            }
        }
        
        if (file_info->status != DECOMP_STATUS_SUCCESS) {
            g_batch_context.files_failed++;
            Logger_Log(LOG_LEVEL_ERROR, "Failed to process file: %s", file_info->input_path);
        }
        
        g_batch_context.files_processed++;
        UpdateBatchProgress(g_batch_context.files_processed, g_batch_context.file_count);
    }
    
    clock_t batch_end = clock();
    g_batch_context.total_processing_time = ((double)(batch_end - batch_start)) / CLOCKS_PER_SEC;
    g_batch_context.end_time = time(NULL);
    
    // Generate report
    GenerateBatchReport();
    
    // Cleanup
    if (g_batch_context.files) {
        free(g_batch_context.files);
        g_batch_context.files = NULL;
    }
    
    Logger_Log(LOG_LEVEL_INFO, "Batch decompression completed");
    Logger_Log(LOG_LEVEL_INFO, "Total files: %d, Successful: %d, Failed: %d, Repaired: %d", 
               g_batch_context.file_count, g_batch_context.files_successful, 
               g_batch_context.files_failed, g_batch_context.files_repaired);
    
    return DECOMP_STATUS_SUCCESS;
}

DecompStatus BatchDecompressor_ProcessAllFiles(const char* input_dir, const char* output_dir) {
    return BatchDecompressor_ProcessDirectory(input_dir, output_dir);
}