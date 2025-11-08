/******************************************************************************
 * Advanced File Compressor - Batch Analyzer
 * Description: Command-line tool for analyzing and decompressing all .comp files
 ******************************************************************************/

#include "../include/decompressor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*============================================================================*/
/* PROGRAM CONSTANTS                                                          */
/*============================================================================*/

#define PROGRAM_NAME        "Advanced File Decompressor"
#define PROGRAM_VERSION     "1.0.0"
#define PROGRAM_DESCRIPTION "Production-grade batch decompression system"

/*============================================================================*/
/* COMMAND LINE OPTIONS                                                       */
/*============================================================================*/

typedef struct {
    char input_directory[1024];
    char output_directory[1024];
    bool auto_repair;
    bool strict_validation;
    bool verify_integrity;
    bool generate_reports;
    bool debug_mode;
    LogLevel log_level;
    bool show_help;
    bool show_version;
} ProgramOptions;

/*============================================================================*/
/* FUNCTION PROTOTYPES                                                        */
/*============================================================================*/

static void PrintUsage(const char* program_name);
static void PrintVersion(void);
static void PrintHelp(const char* program_name);
static int ParseCommandLine(int argc, char* argv[], ProgramOptions* options);
static void InitializeDefaultOptions(ProgramOptions* options);
static void PrintConfiguration(const ProgramOptions* options);
static DecompStatus ProcessFiles(const ProgramOptions* options);
static void PrintFinalReport(const ProcessingStats* stats);

/*============================================================================*/
/* MAIN FUNCTION                                                              */
/*============================================================================*/

int main(int argc, char* argv[]) {
    printf("\n");
    printf("========================================\n");
    printf("%s v%s\n", PROGRAM_NAME, PROGRAM_VERSION);
    printf("%s\n", PROGRAM_DESCRIPTION);
    printf("========================================\n\n");
    
    // Parse command line options
    ProgramOptions options;
    InitializeDefaultOptions(&options);
    
    if (ParseCommandLine(argc, argv, &options) != 0) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    // Handle help and version
    if (options.show_help) {
        PrintHelp(argv[0]);
        return 0;
    }
    
    if (options.show_version) {
        PrintVersion();
        return 0;
    }
    
    // Print configuration
    PrintConfiguration(&options);
    
    // Initialize decompression system
    DecompConfig config;
    config.auto_repair_enabled = options.auto_repair;
    config.strict_validation = options.strict_validation;
    config.verify_integrity = options.verify_integrity;
    config.generate_reports = options.generate_reports;
    config.debug_mode = options.debug_mode;
    config.log_level = options.log_level;
    
    strcpy(config.output_directory, options.output_directory);
    strcpy(config.report_directory, options.output_directory);
    
    printf("Initializing decompression system...\n");
    
    if (Decompressor_Init(&config) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize decompression system\n");
        return 1;
    }
    
    // Process files
    printf("\nStarting batch decompression...\n");
    printf("Input directory: %s\n", options.input_directory);
    printf("Output directory: %s\n", options.output_directory);
    printf("========================================\n\n");
    
    clock_t start_time = clock();
    
    DecompStatus status = ProcessFiles(&options);
    
    clock_t end_time = clock();
    double processing_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    // Get final statistics
    ProcessingStats stats;
    if (Decompressor_GetStats(&stats) == DECOMP_STATUS_SUCCESS) {
        PrintFinalReport(&stats);
        printf("\nTotal processing time: %.2f seconds\n", processing_time);
    }
    
    // Cleanup
    Decompressor_Cleanup();
    
    printf("\nDecompression process completed.\n");
    printf("========================================\n\n");
    
    return (status == DECOMP_STATUS_SUCCESS) ? 0 : 1;
}

/*============================================================================*/
/* COMMAND LINE PARSING                                                       */
/*============================================================================*/

static void InitializeDefaultOptions(ProgramOptions* options) {
    memset(options, 0, sizeof(ProgramOptions));
    
    strcpy(options->input_directory, "./output");
    strcpy(options->output_directory, "./decompressed");
    
    options->auto_repair = true;
    options->strict_validation = true;
    options->verify_integrity = true;
    options->generate_reports = true;
    options->debug_mode = false;
    options->log_level = LOG_LEVEL_INFO;
    
    options->show_help = false;
    options->show_version = false;
}

static int ParseCommandLine(int argc, char* argv[], ProgramOptions* options) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            options->show_help = true;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            options->show_version = true;
        }
        else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (i + 1 < argc) {
                strncpy(options->input_directory, argv[++i], 1024 - 1);
                options->input_directory[1024 - 1] = '\0';
            } else {
                fprintf(stderr, "ERROR: Missing argument for %s\n", argv[i]);
                return -1;
            }
        }
        else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                strncpy(options->output_directory, argv[++i], 1024 - 1);
                options->output_directory[1024 - 1] = '\0';
            } else {
                fprintf(stderr, "ERROR: Missing argument for %s\n", argv[i]);
                return -1;
            }
        }
        else if (strcmp(argv[i], "--no-repair") == 0) {
            options->auto_repair = false;
        }
        else if (strcmp(argv[i], "--no-validation") == 0) {
            options->strict_validation = false;
        }
        else if (strcmp(argv[i], "--no-verify") == 0) {
            options->verify_integrity = false;
        }
        else if (strcmp(argv[i], "--no-reports") == 0) {
            options->generate_reports = false;
        }
        else if (strcmp(argv[i], "--debug") == 0) {
            options->debug_mode = true;
            options->log_level = LOG_LEVEL_DEBUG;
        }
        else if (strcmp(argv[i], "--trace") == 0) {
            options->debug_mode = true;
            options->log_level = LOG_LEVEL_TRACE;
        }
        else if (strcmp(argv[i], "--quiet") == 0) {
            options->log_level = LOG_LEVEL_WARNING;
        }
        else if (strcmp(argv[i], "--verbose") == 0) {
            options->log_level = LOG_LEVEL_DEBUG;
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "ERROR: Unknown option: %s\n", argv[i]);
            return -1;
        }
        else {
            // Treat as input directory if no -i specified
            if (i == 1 && argc > 1) {
                strncpy(options->input_directory, argv[++i], 1024 - 1);
                options->input_directory[1024 - 1] = '\0';
            } else {
                fprintf(stderr, "ERROR: Unexpected argument: %s\n", argv[i]);
                return -1;
            }
        }
    }
    
    return 0;
}

static void PrintUsage(const char* program_name) {
    printf("Usage: %s [OPTIONS] [INPUT_DIRECTORY]\n", program_name);
    printf("       %s -h | --help    for detailed help\n", program_name);
    printf("       %s -v | --version for version information\n", program_name);
}

static void PrintVersion(void) {
    printf("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
    printf("Advanced file decompression system\n");
    printf("Built with production-grade error recovery\n");
}

static void PrintHelp(const char* program_name) {
    PrintVersion();
    printf("\n%s\n\n", PROGRAM_DESCRIPTION);
    
    printf("Usage: %s [OPTIONS] [INPUT_DIRECTORY]\n\n", program_name);
    
    printf("Arguments:\n");
    printf("  INPUT_DIRECTORY         Directory containing .comp files to decompress\n");
    printf("                          (default: ./output)\n\n");
    
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --version           Show version information\n");
    printf("  -i, --input DIR         Input directory (overrides positional argument)\n");
    printf("  -o, --output DIR        Output directory (default: ./decompressed)\n");
    printf("  --no-repair             Disable automatic error repair\n");
    printf("  --no-validation         Disable strict validation\n");
    printf("  --no-verify             Disable integrity verification\n");
    printf("  --no-reports            Disable report generation\n");
    printf("  --debug                 Enable debug mode (detailed logging)\n");
    printf("  --trace                 Enable trace mode (maximum logging)\n");
    printf("  --quiet                 Reduce output (warnings and errors only)\n");
    printf("  --verbose               Increase output (debug level)\n");
    
    printf("\nExamples:\n");
    printf("  %s                      # Decompress files from ./output to ./decompressed\n", program_name);
    printf("  %s /path/to/comp        # Decompress from specific directory\n", program_name);
    printf("  %s -i input -o output   # Specify input and output directories\n", program_name);
    printf("  %s --debug              # Run with debug logging\n", program_name);
    printf("  %s --no-repair          # Disable automatic error repair\n", program_name);
    
    printf("\nSupported File Types:\n");
    printf("  Text:     .txt, .csv, .json, .xml\n");
    printf("  Documents: .pdf, .docx\n");
    printf("  Audio:    .wav, .mp3\n");
    printf("  Images:   .bmp, .png, .jpg, .jpeg\n");
    printf("  Binary:   All other formats\n");
    
    printf("\nCompression Algorithms:\n");
    printf("  Huffman Coding        - Optimized for text files\n");
    printf("  LZ77 Dictionary       - General purpose compression\n");
    printf("  LZW Dictionary        - Table-based compression\n");
    printf("  RLE Encoding          - Run-length encoding\n");
    printf("  Hardcore Multi-stage  - Advanced multi-algorithm\n");
    printf("  Audio Advanced        - Psychoacoustic modeling\n");
    printf("  Image Advanced        - Predictive coding\n");
}

static void PrintConfiguration(const ProgramOptions* options) {
    printf("Configuration:\n");
    printf("  Input Directory:  %s\n", options->input_directory);
    printf("  Output Directory: %s\n", options->output_directory);
    printf("  Auto Repair:      %s\n", options->auto_repair ? "enabled" : "disabled");
    printf("  Strict Validation: %s\n", options->strict_validation ? "enabled" : "disabled");
    printf("  Verify Integrity: %s\n", options->verify_integrity ? "enabled" : "disabled");
    printf("  Generate Reports: %s\n", options->generate_reports ? "enabled" : "disabled");
    printf("  Debug Mode:       %s\n", options->debug_mode ? "enabled" : "disabled");
    printf("  Log Level:        ");
    
    switch (options->log_level) {
        case LOG_LEVEL_ERROR:   printf("ERROR\n"); break;
        case LOG_LEVEL_WARNING: printf("WARNING\n"); break;
        case LOG_LEVEL_INFO:    printf("INFO\n"); break;
        case LOG_LEVEL_DEBUG:   printf("DEBUG\n"); break;
        case LOG_LEVEL_TRACE:   printf("TRACE\n"); break;
        default:                printf("UNKNOWN\n"); break;
    }
    
    printf("\n");
}

static DecompStatus ProcessFiles(const ProgramOptions* options) {
    return Decompressor_DecompressDirectory(options->input_directory, options->output_directory);
}

static void PrintFinalReport(const ProcessingStats* stats) {
    if (!stats) {
        return;
    }
    
    printf("\n========================================\n");
    printf("FINAL PROCESSING REPORT\n");
    printf("========================================\n");
    
    printf("Files Processed:     %llu\n", stats->total_files_processed);
    printf("Successful:          %llu\n", stats->total_files_successful);
    printf("Failed:              %llu\n", stats->total_files_failed);
    
    double success_rate = 0.0;
    if (stats->total_files_processed > 0) {
        success_rate = (stats->total_files_successful * 100.0) / stats->total_files_processed;
    }
    printf("Success Rate:        %.2f%%\n", success_rate);
    
    printf("\nSize Statistics:\n");
    char size_buffer[64];
    
    Utility_FormatFileSize(stats->total_bytes_original, size_buffer, sizeof(size_buffer));
    printf("Original Size:       %s\n", size_buffer);
    
    Utility_FormatFileSize(stats->total_bytes_compressed, size_buffer, sizeof(size_buffer));
    printf("Compressed Size:     %s\n", size_buffer);
    
    Utility_FormatFileSize(stats->total_bytes_decompressed, size_buffer, sizeof(size_buffer));
    printf("Decompressed Size:   %s\n", size_buffer);
    
    printf("Compression Ratio:   %.2f%%\n", stats->average_compression_ratio);
    printf("Processing Time:     %.2f seconds\n", stats->processing_time_seconds);
    
    if (stats->processing_time_seconds > 0) {
        double speed = stats->total_files_processed / stats->processing_time_seconds;
        printf("Processing Speed:    %.2f files/second\n", speed);
    }
    
    printf("========================================\n");
}

/*============================================================================*/
/* PLATFORM-SPECIFIC INCLUDES                                                 */
/*============================================================================*/

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
#endif