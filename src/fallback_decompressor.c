/******************************************************************************
 * Advanced File Compressor - Fallback Decompressor
 * Description: Robust decompression with multiple fallback strategies
 ******************************************************************************/

#include "../include/decompressor.h"
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/*============================================================================*/
/* CONSTANTS                                                                  */
/*============================================================================*/

#define FALLBACK_VERSION        "1.0.0"
#define MAX_FALLBACK_ATTEMPTS   5
#define RAW_COPY_THRESHOLD      0.1  // If decompression ratio < 10%, try raw copy

/*============================================================================*/
/* STRUCTURES                                                                 */
/*============================================================================*/

typedef enum {
    FALLBACK_STRATEGY_NORMAL = 0,
    FALLBACK_STRATEGY_HEADER_REPAIR,
    FALLBACK_STRATEGY_RAW_COPY,
    FALLBACK_STRATEGY_PARTIAL_EXTRACT,
    FALLBACK_STRATEGY_FORCE_ALGORITHM
} FallbackStrategy;

typedef struct {
    char input_file[DECOMP_MAX_PATH];
    char output_file[DECOMP_MAX_PATH];
    FallbackStrategy strategy;
    DecompStatus last_status;
    uint64_t input_size;
    uint64_t output_size;
    bool success;
    char strategy_description[128];
    char error_details[256];
} FallbackAttempt;

/*============================================================================*/
/* PRIVATE FUNCTIONS                                                          */
/*============================================================================*/

static const char* GetStrategyDescription(FallbackStrategy strategy) {
    switch (strategy) {
        case FALLBACK_STRATEGY_NORMAL: return "Normal decompression";
        case FALLBACK_STRATEGY_HEADER_REPAIR: return "Header repair + decompression";
        case FALLBACK_STRATEGY_RAW_COPY: return "Raw data copy";
        case FALLBACK_STRATEGY_PARTIAL_EXTRACT: return "Partial data extraction";
        case FALLBACK_STRATEGY_FORCE_ALGORITHM: return "Force algorithm override";
        default: return "Unknown strategy";
    }
}

static bool AttemptNormalDecompression(FallbackAttempt* attempt) {
    if (!attempt) return false;
    
    strcpy(attempt->strategy_description, "Standard decompression");
    
    // Initialize decompressor with auto-repair enabled
    DecompConfig config = {0};
    config.auto_repair_enabled = true;
    config.verify_integrity = false;
    config.debug_mode = true;
    config.log_level = LOG_LEVEL_DEBUG;
    strcpy(config.output_directory, "./");
    strcpy(config.report_directory, "./logs");
    
    if (Decompressor_Init(&config) != 0) {
        strcpy(attempt->error_details, "Failed to initialize decompressor");
        return false;
    }
    
    attempt->last_status = Decompressor_DecompressFile(attempt->input_file, attempt->output_file);
    
    Decompressor_Cleanup();
    
    // Check if output file was created
    FILE* output_file = fopen(attempt->output_file, "rb");
    if (output_file) {
        fseek(output_file, 0, SEEK_END);
        attempt->output_size = ftell(output_file);
        fclose(output_file);
        
        if (attempt->output_size > 0) {
            attempt->success = true;
            return true;
        }
    }
    
    snprintf(attempt->error_details, sizeof(attempt->error_details),
             "Decompression failed: %s", Utility_GetStatusDescription(attempt->last_status));
    return false;
}

static bool AttemptRawCopy(FallbackAttempt* attempt) {
    if (!attempt) return false;
    strcpy(attempt->strategy_description, "Raw data copy disabled (enforce decode-only)");
    strcpy(attempt->error_details, "Raw copy fallback is disabled to prevent silent data loss");
    return false;
}

static bool AttemptPartialExtraction(FallbackAttempt* attempt) {
    if (!attempt) return false;
    
    strcpy(attempt->strategy_description, "Partial data extraction");
    
    FILE* input = fopen(attempt->input_file, "rb");
    if (!input) {
        strcpy(attempt->error_details, "Failed to open input file");
        return false;
    }
    
    FILE* output = fopen(attempt->output_file, "wb");
    if (!output) {
        strcpy(attempt->error_details, "Failed to create output file");
        fclose(input);
        return false;
    }
    
    // Try to extract readable data from the file
    uint8_t buffer[8192];
    size_t bytes_read;
    size_t total_extracted = 0;
    
    // Skip first 64 bytes (potential header)
    fseek(input, 64, SEEK_SET);
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        // Filter out obviously compressed/binary data
        // Look for patterns that might be text or structured data
        size_t valid_bytes = 0;
        
        for (size_t i = 0; i < bytes_read; i++) {
            // Keep printable ASCII, newlines, tabs
            if ((buffer[i] >= 32 && buffer[i] <= 126) || 
                buffer[i] == '\n' || buffer[i] == '\r' || buffer[i] == '\t') {
                buffer[valid_bytes++] = buffer[i];
            }
        }
        
        if (valid_bytes > 0) {
            fwrite(buffer, 1, valid_bytes, output);
            total_extracted += valid_bytes;
        }
        
        // Stop if we've extracted a reasonable amount
        if (total_extracted > 10000) break;
    }
    
    fclose(input);
    fclose(output);
    
    if (total_extracted > 50) {
        attempt->output_size = total_extracted;
        attempt->success = true;
        
        snprintf(attempt->strategy_description, sizeof(attempt->strategy_description),
                 "Partial extraction (%lu bytes)", (unsigned long)total_extracted);
        return true;
    } else {
        strcpy(attempt->error_details, "No extractable data found");
        return false;
    }
}

/*============================================================================*/
/* PUBLIC FUNCTIONS                                                           */
/*============================================================================*/

int FallbackDecompressor_ProcessFile(const char* input_file, const char* output_file) {
    if (!input_file || !output_file) {
        printf("Error: Invalid file paths\n");
        return -1;
    }
    
    printf("Advanced File Compressor - Fallback Decompressor v%s\n", FALLBACK_VERSION);
    printf("Processing: %s -> %s\n", input_file, output_file);
    
    // Get input file size
    FILE* input = fopen(input_file, "rb");
    if (!input) {
        printf("Error: Cannot open input file: %s\n", input_file);
        return -1;
    }
    
    fseek(input, 0, SEEK_END);
    uint64_t input_size = ftell(input);
    fclose(input);
    
    printf("Input file size: %llu bytes\n", input_size);
    
    FallbackAttempt attempts[MAX_FALLBACK_ATTEMPTS];
    memset(attempts, 0, sizeof(attempts));
    
    // Initialize all attempts
    for (int i = 0; i < MAX_FALLBACK_ATTEMPTS; i++) {
        strncpy(attempts[i].input_file, input_file, sizeof(attempts[i].input_file) - 1);
        strncpy(attempts[i].output_file, output_file, sizeof(attempts[i].output_file) - 1);
        attempts[i].input_size = input_size;
    }
    
    // Strategy 1: Normal decompression
    printf("\n[ATTEMPT 1] Normal decompression...\n");
    attempts[0].strategy = FALLBACK_STRATEGY_NORMAL;
    if (AttemptNormalDecompression(&attempts[0])) {
        printf("✓ Success: %s\n", attempts[0].strategy_description);
        printf("  Output size: %llu bytes\n", attempts[0].output_size);
        return 0;
    } else {
        printf("✗ Failed: %s\n", attempts[0].error_details);
    }
    
    // Strategy 2: Raw copy disabled
    printf("\n[ATTEMPT 2] Raw data copy (disabled)\n");
    attempts[1].strategy = FALLBACK_STRATEGY_RAW_COPY;
    AttemptRawCopy(&attempts[1]);
    printf("✗ Skipped: %s\n", attempts[1].error_details);
    
    // Strategy 3: Partial extraction
    printf("\n[ATTEMPT 3] Partial data extraction...\n");
    attempts[2].strategy = FALLBACK_STRATEGY_PARTIAL_EXTRACT;
    if (AttemptPartialExtraction(&attempts[2])) {
        printf("✓ Success: %s\n", attempts[2].strategy_description);
        printf("  Output size: %llu bytes\n", attempts[2].output_size);
        return 0;
    } else {
        printf("✗ Failed: %s\n", attempts[2].error_details);
    }
    
    printf("\n✗ All fallback strategies failed\n");
    printf("Recommendations:\n");
    printf("  - File may be severely corrupted\n");
    printf("  - Try manual hex inspection\n");
    printf("  - Check if file is actually compressed\n");
    
    return -1;
}

int FallbackDecompressor_ProcessDirectory(const char* input_dir, const char* output_dir) {
    if (!input_dir || !output_dir) {
        printf("Error: Invalid directory paths\n");
        return -1;
    }
    
    printf("Advanced File Compressor - Fallback Decompressor v%s\n", FALLBACK_VERSION);
    printf("Batch processing directory: %s -> %s\n", input_dir, output_dir);
    
    // Create output directory
#ifdef _WIN32
    _mkdir(output_dir);
#else
    mkdir(output_dir, 0777);
#endif
    
    int total_files = 0;
    int successful_files = 0;
    
    // Scan directory for .comp files
#ifdef _WIN32
    char search_pattern[DECOMP_MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.comp", input_dir);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFile(search_pattern, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        printf("No .comp files found in directory: %s\n", input_dir);
        return 0;
    }
    
    do {
        char input_path[DECOMP_MAX_PATH];
        char output_path[DECOMP_MAX_PATH];
        
        snprintf(input_path, sizeof(input_path), "%s\\%s", input_dir, find_data.cFileName);
        
        // Generate output filename (remove .comp extension)
        char output_filename[DECOMP_MAX_FILENAME];
        strncpy(output_filename, find_data.cFileName, sizeof(output_filename) - 1);
        size_t len = strlen(output_filename);
        if (len > 5 && strcasecmp(output_filename + len - 5, ".comp") == 0) {
            output_filename[len - 5] = '\0';
        }
        
        snprintf(output_path, sizeof(output_path), "%s\\%s", output_dir, output_filename);
        
        printf("\n============================================================\n");
        printf("Processing file %d: %s\n", total_files + 1, find_data.cFileName);
        
        if (FallbackDecompressor_ProcessFile(input_path, output_path) == 0) {
            successful_files++;
        }
        
        total_files++;
        
    } while (FindNextFile(find_handle, &find_data));
    
    FindClose(find_handle);
#endif
    
    printf("\n============================================================\n");
    printf("BATCH PROCESSING SUMMARY\n");
    printf("Total files processed: %d\n", total_files);
    printf("Successful: %d\n", successful_files);
    printf("Failed: %d\n", total_files - successful_files);
    
    if (total_files > 0) {
        double success_rate = ((double)successful_files / total_files) * 100.0;
        printf("Success rate: %.1f%%\n", success_rate);
    }
    
    return (successful_files == total_files) ? 0 : 1;
}