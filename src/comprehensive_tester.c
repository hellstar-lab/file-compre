/******************************************************************************
 * Advanced File Compressor - Comprehensive Tester
 * Description: Production-grade testing and verification system
 ******************************************************************************/

#include "../include/decompressor.h"
#include <string.h>
#include <ctype.h>
#include <time.h>

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

#define TESTER_VERSION          "1.0.0"
#define MAX_TEST_FILES          100
#define RETRY_LIMIT             3
#define BUFFER_SIZE             65536

/*============================================================================*/
/* STRUCTURES                                                                 */
/*============================================================================*/

typedef struct {
    char input_file[DECOMP_MAX_PATH];
    char output_file[DECOMP_MAX_PATH];
    char original_file[DECOMP_MAX_PATH];
    char algorithm_name[32];
    uint8_t algorithm;
    uint64_t original_size;
    uint64_t compressed_size;
    uint64_t decompressed_size;
    uint32_t original_crc32;
    uint32_t decompressed_crc32;
    DecompStatus status;
    int retry_count;
    bool fallback_used;
    bool bit_perfect;
    double processing_time;
    char error_message[256];
    char fallback_details[256];
} TestResult;

typedef struct {
    TestResult* results;
    int test_count;
    int passed;
    int failed;
    int fallback_recoveries;
    int bit_perfect_matches;
    double total_time;
    char input_directory[DECOMP_MAX_PATH];
    char output_directory[DECOMP_MAX_PATH];
    char original_directory[DECOMP_MAX_PATH];
    char log_directory[DECOMP_MAX_PATH];
    char report_path[DECOMP_MAX_PATH];
} TestContext;

/*============================================================================*/
/* GLOBAL VARIABLES                                                           */
/*============================================================================*/

static TestContext g_test_context = {0};

/*============================================================================*/
/* PRIVATE FUNCTIONS                                                          */
/*============================================================================*/

static void InitializeLogging(void) {
    // Create log directories
#ifdef _WIN32
    _mkdir("logs");
    _mkdir("logs\\decompress_debug");
    _mkdir("logs\\run_diagnostics");
    _mkdir("logs\\hardcore_debug");
    _mkdir("restored");
#else
    mkdir("logs", 0777);
    mkdir("logs/decompress_debug", 0777);
    mkdir("logs/run_diagnostics", 0777);
    mkdir("logs/hardcore_debug", 0777);
    mkdir("restored", 0777);
#endif
    
    // Initialize main debug log
    FILE* debug_log = fopen("logs/decompress_debug.log", "w");
    if (debug_log) {
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        
        fprintf(debug_log, "Advanced File Compressor - Comprehensive Test Log\n");
        fprintf(debug_log, "================================================\n");
        fprintf(debug_log, "Test Started: %s\n", timestamp);
        fprintf(debug_log, "Version: %s\n\n", TESTER_VERSION);
        fclose(debug_log);
    }
}

static void LogTestStart(const TestResult* test) {
    FILE* debug_log = fopen("logs/decompress_debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "\n[TEST START] %s\n", test->input_file);
        fprintf(debug_log, "  Algorithm: %d (%s)\n", test->algorithm, test->algorithm_name);
        fprintf(debug_log, "  Original Size: %llu bytes\n", test->original_size);
        fprintf(debug_log, "  Compressed Size: %llu bytes\n", test->compressed_size);
        fprintf(debug_log, "  Output: %s\n", test->output_file);
        fclose(debug_log);
    }
}

static void LogTestResult(const TestResult* test) {
    FILE* debug_log = fopen("logs/decompress_debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "\n[TEST RESULT] %s\n", test->input_file);
        fprintf(debug_log, "  Status: %s\n", Utility_GetStatusDescription(test->status));
        fprintf(debug_log, "  Decompressed Size: %llu bytes\n", test->decompressed_size);
        fprintf(debug_log, "  Processing Time: %.3f seconds\n", test->processing_time);
        fprintf(debug_log, "  Retry Count: %d\n", test->retry_count);
        fprintf(debug_log, "  Fallback Used: %s\n", test->fallback_used ? "YES" : "NO");
        fprintf(debug_log, "  Bit Perfect: %s\n", test->bit_perfect ? "YES" : "NO");
        
        if (test->original_crc32 != 0 || test->decompressed_crc32 != 0) {
            fprintf(debug_log, "  Original CRC32: 0x%08X\n", test->original_crc32);
            fprintf(debug_log, "  Decompressed CRC32: 0x%08X\n", test->decompressed_crc32);
        }
        
        if (strlen(test->error_message) > 0) {
            fprintf(debug_log, "  Error: %s\n", test->error_message);
        }
        
        if (strlen(test->fallback_details) > 0) {
            fprintf(debug_log, "  Fallback: %s\n", test->fallback_details);
        }
        
        fclose(debug_log);
    }
}

static bool FindOriginalFile(const char* comp_filename, const char* original_dir, char* original_path) {
    if (!comp_filename || !original_dir || !original_path) return false;
    
    // Remove .comp extension to get base name
    char base_name[DECOMP_MAX_FILENAME];
    strncpy(base_name, comp_filename, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    
    size_t len = strlen(base_name);
    if (len > 5 && strcasecmp(base_name + len - 5, ".comp") == 0) {
        base_name[len - 5] = '\0';
    }
    
    // Common file extensions to try
    const char* extensions[] = {".pdf", ".txt", ".json", ".xml", ".csv", ".mp3", ".jpg", ".png", ""};
    
    for (int i = 0; extensions[i][0] != '\0' || i == 0; i++) {
        snprintf(original_path, DECOMP_MAX_PATH, "%s/%s%s", original_dir, base_name, extensions[i]);
        
        FILE* test_file = fopen(original_path, "rb");
        if (test_file) {
            fclose(test_file);
            return true;
        }
        
        if (extensions[i][0] == '\0') break; // Last attempt with no extension
    }
    
    return false;
}

static bool PerformDecompression(TestResult* test) {
    if (!test) return false;
    
    clock_t start_time = clock();
    
    // Initialize decompressor
    DecompConfig config = {0};
    config.auto_repair_enabled = true;
    config.verify_integrity = true;
    config.debug_mode = true;
    config.log_level = LOG_LEVEL_DEBUG;
    strncpy(config.output_directory, g_test_context.output_directory, sizeof(config.output_directory) - 1);
    strncpy(config.report_directory, g_test_context.log_directory, sizeof(config.report_directory) - 1);
    
    if (Decompressor_Init(&config) != 0) {
        strcpy(test->error_message, "Failed to initialize decompressor");
        return false;
    }
    
    // Attempt decompression with retries
    for (int retry = 0; retry <= RETRY_LIMIT; retry++) {
        test->retry_count = retry;
        
        test->status = Decompressor_DecompressFile(test->input_file, test->output_file);
        
        if (test->status == DECOMP_STATUS_SUCCESS) {
            // Check if output file exists and get size
            FILE* output_file = fopen(test->output_file, "rb");
            if (output_file) {
                fseek(output_file, 0, SEEK_END);
                test->decompressed_size = ftell(output_file);
                fclose(output_file);
                
                if (retry > 0) {
                    test->fallback_used = true;
                    snprintf(test->fallback_details, sizeof(test->fallback_details),
                             "Recovered after %d retries", retry);
                }
                break;
            } else {
                test->status = DECOMP_STATUS_IO_ERROR;
                strcpy(test->error_message, "Output file not created");
            }
        }
        
        if (retry < RETRY_LIMIT) {
            // Log retry attempt
            FILE* debug_log = fopen("logs/decompress_debug.log", "a");
            if (debug_log) {
                fprintf(debug_log, "  [RETRY %d] Status: %s\n", retry + 1, 
                        Utility_GetStatusDescription(test->status));
                fclose(debug_log);
            }
        }
    }
    
    clock_t end_time = clock();
    test->processing_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    Decompressor_Cleanup();
    
    return (test->status == DECOMP_STATUS_SUCCESS);
}

static bool VerifyBitPerfect(TestResult* test) {
    if (!test || strlen(test->original_file) == 0) return false;
    
    // Calculate CRC32 of original file
    test->original_crc32 = CRC32_CalculateFile(test->original_file);
    if (test->original_crc32 == 0) {
        strcpy(test->error_message, "Failed to calculate original CRC32");
        return false;
    }
    
    // Calculate CRC32 of decompressed file
    test->decompressed_crc32 = CRC32_CalculateFile(test->output_file);
    if (test->decompressed_crc32 == 0) {
        strcpy(test->error_message, "Failed to calculate decompressed CRC32");
        return false;
    }
    
    // Compare CRC32 values
    test->bit_perfect = (test->original_crc32 == test->decompressed_crc32);
    
    if (!test->bit_perfect) {
        snprintf(test->error_message, sizeof(test->error_message),
                 "CRC32 mismatch: original=0x%08X, decompressed=0x%08X",
                 test->original_crc32, test->decompressed_crc32);
    }
    
    return test->bit_perfect;
}

static void GenerateComprehensiveReport(void) {
    FILE* report = fopen(g_test_context.report_path, "w");
    if (!report) {
        printf("Error: Failed to create comprehensive report\n");
        return;
    }
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(report, "Advanced File Compressor - Comprehensive Test Report\n");
    fprintf(report, "===================================================\n\n");
    fprintf(report, "Test Date: %s\n", timestamp);
    fprintf(report, "Version: %s\n\n", TESTER_VERSION);
    
    fprintf(report, "Test Configuration:\n");
    fprintf(report, "-------------------\n");
    fprintf(report, "Input Directory: %s\n", g_test_context.input_directory);
    fprintf(report, "Output Directory: %s\n", g_test_context.output_directory);
    fprintf(report, "Original Directory: %s\n", g_test_context.original_directory);
    fprintf(report, "Log Directory: %s\n", g_test_context.log_directory);
    fprintf(report, "Retry Limit: %d\n\n", RETRY_LIMIT);
    
    fprintf(report, "Summary:\n");
    fprintf(report, "--------\n");
    fprintf(report, "Total Files Tested: %d\n", g_test_context.test_count);
    fprintf(report, "Passed: %d\n", g_test_context.passed);
    fprintf(report, "Failed: %d\n", g_test_context.failed);
    fprintf(report, "Fallback Recoveries: %d\n", g_test_context.fallback_recoveries);
    fprintf(report, "Bit-Perfect Matches: %d\n", g_test_context.bit_perfect_matches);
    fprintf(report, "Total Processing Time: %.3f seconds\n", g_test_context.total_time);
    
    if (g_test_context.test_count > 0) {
        double success_rate = ((double)g_test_context.passed / g_test_context.test_count) * 100.0;
        double bit_perfect_rate = ((double)g_test_context.bit_perfect_matches / g_test_context.test_count) * 100.0;
        fprintf(report, "Success Rate: %.1f%%\n", success_rate);
        fprintf(report, "Bit-Perfect Rate: %.1f%%\n", bit_perfect_rate);
    }
    
    fprintf(report, "\nDetailed Results:\n");
    fprintf(report, "=================\n");
    fprintf(report, "%-30s %-15s %-10s %-12s %-12s %-8s %-8s %-10s %-15s\n",
            "Filename", "Algorithm", "Status", "Original", "Decompressed", "Time(s)", "Retries", "Fallback", "Bit-Perfect");
    fprintf(report, "%s\n", "----------------------------------------------------------------------------------------------------------------------------");
    
    for (int i = 0; i < g_test_context.test_count; i++) {
        const TestResult* test = &g_test_context.results[i];
        const char* filename = strrchr(test->input_file, '/');
        if (!filename) filename = strrchr(test->input_file, '\\');
        if (!filename) filename = test->input_file;
        else filename++;
        
        fprintf(report, "%-30s %-15s %-10s %-12llu %-12llu %-8.3f %-8d %-10s %-15s\n",
                filename,
                test->algorithm_name,
                Utility_GetStatusDescription(test->status),
                test->original_size,
                test->decompressed_size,
                test->processing_time,
                test->retry_count,
                test->fallback_used ? "YES" : "NO",
                test->bit_perfect ? "YES" : "NO");
        
        if (strlen(test->error_message) > 0) {
            fprintf(report, "    Error: %s\n", test->error_message);
        }
        
        if (strlen(test->fallback_details) > 0) {
            fprintf(report, "    Fallback: %s\n", test->fallback_details);
        }
    }
    
    fprintf(report, "\nRecommendations:\n");
    fprintf(report, "================\n");
    
    if (g_test_context.failed > 0) {
        fprintf(report, "- %d files failed decompression - check logs/decompress_debug.log for details\n", g_test_context.failed);
    }
    
    if (g_test_context.fallback_recoveries > 0) {
        fprintf(report, "- %d files required fallback recovery - consider improving primary algorithms\n", g_test_context.fallback_recoveries);
    }
    
    if (g_test_context.bit_perfect_matches < g_test_context.passed) {
        int mismatches = g_test_context.passed - g_test_context.bit_perfect_matches;
        fprintf(report, "- %d files have CRC32 mismatches - verify compression/decompression integrity\n", mismatches);
    }
    
    fprintf(report, "- Check logs/hardcore_debug/lzma_debug.log for LZMA-specific issues\n");
    fprintf(report, "- Review logs/run_diagnostics/ for individual file reports\n");
    
    fclose(report);
    printf("[INFO] Comprehensive report generated: %s\n", g_test_context.report_path);
}

/*============================================================================*/
/* PUBLIC FUNCTIONS                                                           */
/*============================================================================*/

int ComprehensiveTester_RunTests(const char* input_dir, const char* output_dir, 
                                const char* original_dir, const char* log_dir) {
    if (!input_dir || !output_dir) {
        printf("Error: Invalid directory parameters\n");
        return -1;
    }
    
    printf("Advanced File Compressor - Comprehensive Tester v%s\n", TESTER_VERSION);
    printf("==================================================\n");
    printf("Input Directory: %s\n", input_dir);
    printf("Output Directory: %s\n", output_dir);
    if (original_dir) printf("Original Directory: %s\n", original_dir);
    if (log_dir) printf("Log Directory: %s\n", log_dir);
    printf("\n");
    
    // Initialize context
    memset(&g_test_context, 0, sizeof(TestContext));
    strncpy(g_test_context.input_directory, input_dir, sizeof(g_test_context.input_directory) - 1);
    strncpy(g_test_context.output_directory, output_dir, sizeof(g_test_context.output_directory) - 1);
    
    if (original_dir) {
        strncpy(g_test_context.original_directory, original_dir, sizeof(g_test_context.original_directory) - 1);
    }
    
    if (log_dir) {
        strncpy(g_test_context.log_directory, log_dir, sizeof(g_test_context.log_directory) - 1);
    } else {
        strcpy(g_test_context.log_directory, "logs");
    }
    
    snprintf(g_test_context.report_path, sizeof(g_test_context.report_path),
             "%s/comprehensive_test_report.txt", g_test_context.log_directory);
    
    // Initialize logging
    InitializeLogging();
    
    // Allocate results array
    g_test_context.results = (TestResult*)malloc(MAX_TEST_FILES * sizeof(TestResult));
    if (!g_test_context.results) {
        printf("Error: Memory allocation failed\n");
        return -1;
    }
    
    // Run file analysis first
    printf("[PHASE 1] Analyzing .comp files...\n");
    int analyzed_files = FileAnalyzer_AnalyzeDirectory(input_dir, NULL);
    if (analyzed_files <= 0) {
        printf("No .comp files found to test\n");
        free(g_test_context.results);
        return 0;
    }
    
    printf("[PHASE 2] Running decompression tests...\n");
    
    // Process each .comp file
    clock_t total_start = clock();
    
    // Scan directory for .comp files
#ifdef _WIN32
    char search_pattern[DECOMP_MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.comp", input_dir);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFile(search_pattern, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (g_test_context.test_count >= MAX_TEST_FILES) break;
            
            TestResult* test = &g_test_context.results[g_test_context.test_count];
            memset(test, 0, sizeof(TestResult));
            
            // Set up file paths
            snprintf(test->input_file, sizeof(test->input_file), "%s\\%s", input_dir, find_data.cFileName);
            
            char output_filename[DECOMP_MAX_FILENAME];
            strncpy(output_filename, find_data.cFileName, sizeof(output_filename) - 1);
            size_t len = strlen(output_filename);
            if (len > 5 && strcasecmp(output_filename + len - 5, ".comp") == 0) {
                output_filename[len - 5] = '\0';
            }
            snprintf(test->output_file, sizeof(test->output_file), "%s\\%s", output_dir, output_filename);
            
            // Find original file for comparison
            if (original_dir && FindOriginalFile(find_data.cFileName, original_dir, test->original_file)) {
                // Original file found for verification
            }
            
            // Analyze file to get algorithm info
            FileAnalyzer_AnalyzeSingle(test->input_file);
            
            printf("[TEST %d/%d] %s\n", g_test_context.test_count + 1, analyzed_files, find_data.cFileName);
            
            LogTestStart(test);
            
            // Perform decompression
            bool success = PerformDecompression(test);
            
            if (success) {
                g_test_context.passed++;
                
                if (test->fallback_used) {
                    g_test_context.fallback_recoveries++;
                }
                
                // Verify bit-perfect restoration if original file available
                if (strlen(test->original_file) > 0) {
                    if (VerifyBitPerfect(test)) {
                        g_test_context.bit_perfect_matches++;
                    }
                }
                
                printf("  ✓ SUCCESS - Decompressed %llu bytes in %.3f seconds\n", 
                       test->decompressed_size, test->processing_time);
                
                if (test->fallback_used) {
                    printf("  ⚠ Fallback recovery used\n");
                }
                
                if (strlen(test->original_file) > 0) {
                    printf("  %s Bit-perfect verification\n", test->bit_perfect ? "✓" : "✗");
                }
                
            } else {
                g_test_context.failed++;
                printf("  ✗ FAILED - %s\n", test->error_message);
            }
            
            LogTestResult(test);
            g_test_context.total_time += test->processing_time;
            g_test_context.test_count++;
            
        } while (FindNextFile(find_handle, &find_data));
        
        FindClose(find_handle);
    }
#endif
    
    clock_t total_end = clock();
    g_test_context.total_time = ((double)(total_end - total_start)) / CLOCKS_PER_SEC;
    
    printf("\n[PHASE 3] Generating reports...\n");
    GenerateComprehensiveReport();
    
    printf("\n[SUMMARY] Test Results:\n");
    printf("  Total Files: %d\n", g_test_context.test_count);
    printf("  Passed: %d\n", g_test_context.passed);
    printf("  Failed: %d\n", g_test_context.failed);
    printf("  Fallback Recoveries: %d\n", g_test_context.fallback_recoveries);
    printf("  Bit-Perfect Matches: %d\n", g_test_context.bit_perfect_matches);
    printf("  Total Time: %.3f seconds\n", g_test_context.total_time);
    
    if (g_test_context.test_count > 0) {
        double success_rate = ((double)g_test_context.passed / g_test_context.test_count) * 100.0;
        printf("  Success Rate: %.1f%%\n", success_rate);
        
        if (g_test_context.bit_perfect_matches > 0) {
            double bit_perfect_rate = ((double)g_test_context.bit_perfect_matches / g_test_context.test_count) * 100.0;
            printf("  Bit-Perfect Rate: %.1f%%\n", bit_perfect_rate);
        }
    }
    
    free(g_test_context.results);
    
    return (g_test_context.failed == 0) ? 0 : 1;
}