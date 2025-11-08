/******************************************************************************
 * Advanced File Compressor - Production Tester
 * Description: Complete production-grade testing and verification system
 ******************************************************************************/

#include "../include/decompressor.h"
#include <string.h>
#include <ctype.h>

/*============================================================================*/
/* CONSTANTS                                                                  */
/*============================================================================*/

#define PRODUCTION_TESTER_VERSION   "1.0.0"

/*============================================================================*/
/* STRUCTURES                                                                 */
/*============================================================================*/

typedef struct {
    bool analyze_only;
    bool comprehensive_test;
    bool verify_integrity;
    bool auto_repair;
    bool debug_mode;
    bool verbose;
    bool self_test;
    char input_directory[DECOMP_MAX_PATH];
    char output_directory[DECOMP_MAX_PATH];
    char original_directory[DECOMP_MAX_PATH];
    char log_directory[DECOMP_MAX_PATH];
    LogLevel log_level;
} TestConfig;

/*============================================================================*/
/* PRIVATE FUNCTIONS                                                          */
/*============================================================================*/

static void PrintUsage(const char* program_name) {
    printf("Advanced File Compressor - Production Tester v%s\n", PRODUCTION_TESTER_VERSION);
    printf("Usage: %s [options] <input_dir> [output_dir] [original_dir]\n\n", program_name);
    
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --version           Show version information\n");
    printf("  -a, --analyze           Analyze files only (no decompression)\n");
    printf("  -t, --test              Run comprehensive tests\n");
    printf("  -i, --verify            Enable integrity verification\n");
    printf("  -r, --repair            Enable auto-repair mode\n");
    printf("  -d, --debug             Enable debug mode\n");
    printf("  -V, --verbose           Enable verbose logging\n");
    printf("  -s, --self-test         Run self-tests\n");
    printf("  -q, --quiet             Quiet mode (errors only)\n");
    
    printf("\nArguments:\n");
    printf("  input_dir               Directory containing .comp files\n");
    printf("  output_dir              Directory for decompressed files (default: ./restored)\n");
    printf("  original_dir            Directory with original files for verification (optional)\n");
    
    printf("\nExamples:\n");
    printf("  %s output/                          # Analyze .comp files in output/\n", program_name);
    printf("  %s -a output/                       # Analysis only\n", program_name);
    printf("  %s -t output/ restored/             # Comprehensive test\n", program_name);
    printf("  %s -t -i output/ restored/ data/    # Test with integrity verification\n", program_name);
    printf("  %s -r -d output/ restored/          # Test with auto-repair and debug\n", program_name);
    printf("  %s -s                               # Run self-tests\n", program_name);
    
    printf("\nOutput:\n");
    printf("  - Analysis reports in logs/\n");
    printf("  - Decompressed files in output_dir\n");
    printf("  - Debug logs in logs/decompress_debug.log\n");
    printf("  - LZMA debug in logs/hardcore_debug/lzma_debug.log\n");
    printf("  - Comprehensive report in logs/comprehensive_test_report.txt\n");
}

static void PrintVersion(void) {
    printf("Advanced File Compressor - Production Tester\n");
    printf("Version: %s\n", PRODUCTION_TESTER_VERSION);
    printf("Built: %s %s\n", __DATE__, __TIME__);
    printf("Features: File Analysis, Comprehensive Testing, Bit-Perfect Verification\n");
}

static bool ParseCommandLine(int argc, char* argv[], TestConfig* config) {
    if (!config) return false;
    
    // Initialize defaults
    memset(config, 0, sizeof(TestConfig));
    config->verify_integrity = true;
    config->auto_repair = true;
    config->log_level = LOG_LEVEL_INFO;
    strcpy(config->output_directory, "./restored");
    strcpy(config->log_directory, "./logs");
    
    if (argc < 2) {
        PrintUsage(argv[0]);
        return false;
    }
    
    int arg_index = 1;
    while (arg_index < argc) {
        const char* arg = argv[arg_index];
        
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            PrintUsage(argv[0]);
            return false;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            PrintVersion();
            return false;
        } else if (strcmp(arg, "-a") == 0 || strcmp(arg, "--analyze") == 0) {
            config->analyze_only = true;
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--test") == 0) {
            config->comprehensive_test = true;
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--verify") == 0) {
            config->verify_integrity = true;
        } else if (strcmp(arg, "-r") == 0 || strcmp(arg, "--repair") == 0) {
            config->auto_repair = true;
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--debug") == 0) {
            config->debug_mode = true;
            config->log_level = LOG_LEVEL_DEBUG;
        } else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--verbose") == 0) {
            config->verbose = true;
            config->log_level = LOG_LEVEL_DEBUG;
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--self-test") == 0) {
            config->self_test = true;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
            config->log_level = LOG_LEVEL_ERROR;
        } else if (arg[0] != '-') {
            // Directory arguments
            if (strlen(config->input_directory) == 0) {
                strncpy(config->input_directory, arg, sizeof(config->input_directory) - 1);
            } else if (strlen(config->output_directory) == 0 || strcmp(config->output_directory, "./restored") == 0) {
                strncpy(config->output_directory, arg, sizeof(config->output_directory) - 1);
            } else if (strlen(config->original_directory) == 0) {
                strncpy(config->original_directory, arg, sizeof(config->original_directory) - 1);
            } else {
                printf("Error: Too many directory arguments\n");
                return false;
            }
        } else {
            printf("Error: Unknown option: %s\n", arg);
            return false;
        }
        
        arg_index++;
    }
    
    // Validate arguments
    if (config->self_test) {
        return true; // Self-test mode doesn't need directories
    }
    
    if (strlen(config->input_directory) == 0) {
        printf("Error: Input directory required\n");
        return false;
    }
    
    // If not analyze-only and not comprehensive test, default to comprehensive test
    if (!config->analyze_only && !config->comprehensive_test) {
        config->comprehensive_test = true;
    }
    
    return true;
}

static bool RunSelfTests(void) {
    printf("Running production-grade self-tests...\n");
    printf("=====================================\n");
    
    bool all_passed = true;
    
    // Test CRC32
    printf("Testing CRC32 module... ");
    if (CRC32_SelfTest()) {
        printf("✓ PASS\n");
    } else {
        printf("✗ FAIL\n");
        all_passed = false;
    }
    
    // Test BitReader
    printf("Testing BitReader module... ");
    const uint8_t test_data[] = {0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x9A};
    BitReader* br = BitReader_CreateFromBuffer(test_data, sizeof(test_data));
    if (br) {
        uint32_t first_byte = BitReader_ReadBits(br, 8);
        uint32_t nibbles = BitReader_ReadBits(br, 4) << 4 | BitReader_ReadBits(br, 4);
        
        if (first_byte == 0xAB && nibbles == 0xCD && !BitReader_HasError(br)) {
            printf("✓ PASS\n");
        } else {
            printf("✗ FAIL (got: 0x%02X, 0x%02X)\n", first_byte, nibbles);
            all_passed = false;
        }
        
        BitReader_Destroy(br);
    } else {
        printf("✗ FAIL (creation failed)\n");
        all_passed = false;
    }
    
    // Test decompressor initialization
    printf("Testing decompressor initialization... ");
    DecompConfig decomp_config = {0};
    decomp_config.auto_repair_enabled = true;
    decomp_config.verify_integrity = true;
    decomp_config.log_level = LOG_LEVEL_ERROR;
    strcpy(decomp_config.output_directory, "./test_output");
    strcpy(decomp_config.report_directory, "./test_logs");
    
    if (Decompressor_Init(&decomp_config) == 0) {
        printf("✓ PASS\n");
        Decompressor_Cleanup();
    } else {
        printf("✗ FAIL\n");
        all_passed = false;
    }
    
    // Test file analyzer on a simple case
    printf("Testing file analyzer... ");
    // This would need an actual .comp file to test properly
    printf("⚠ SKIP (requires .comp file)\n");
    
    printf("\nSelf-test summary: %s\n", all_passed ? "✓ ALL TESTS PASSED" : "✗ SOME TESTS FAILED");
    return all_passed;
}

static int RunAnalysisOnly(const TestConfig* config) {
    printf("[ANALYSIS MODE] Analyzing .comp files in: %s\n", config->input_directory);
    
    int result = FileAnalyzer_AnalyzeDirectory(config->input_directory, NULL);
    
    if (result > 0) {
        printf("\n✓ Analysis completed successfully\n");
        printf("  Files analyzed: %d\n", result);
        printf("  Report generated in logs/\n");
        return 0;
    } else if (result == 0) {
        printf("\n⚠ No .comp files found in directory\n");
        return 0;
    } else {
        printf("\n✗ Analysis failed\n");
        return 1;
    }
}

static int RunComprehensiveTest(const TestConfig* config) {
    printf("[COMPREHENSIVE TEST MODE]\n");
    printf("Input Directory: %s\n", config->input_directory);
    printf("Output Directory: %s\n", config->output_directory);
    if (strlen(config->original_directory) > 0) {
        printf("Original Directory: %s\n", config->original_directory);
    }
    printf("Auto-repair: %s\n", config->auto_repair ? "ON" : "OFF");
    printf("Integrity verification: %s\n", config->verify_integrity ? "ON" : "OFF");
    printf("Debug mode: %s\n", config->debug_mode ? "ON" : "OFF");
    printf("\n");
    
    const char* original_dir = strlen(config->original_directory) > 0 ? config->original_directory : NULL;
    
    int result = ComprehensiveTester_RunTests(config->input_directory, 
                                            config->output_directory,
                                            original_dir,
                                            config->log_directory);
    
    if (result == 0) {
        printf("\n✓ All tests passed successfully\n");
        printf("  Check logs/ for detailed reports\n");
        printf("  Decompressed files in %s\n", config->output_directory);
        return 0;
    } else {
        printf("\n✗ Some tests failed\n");
        printf("  Check logs/decompress_debug.log for details\n");
        printf("  Check logs/comprehensive_test_report.txt for summary\n");
        return 1;
    }
}

/*============================================================================*/
/* MAIN FUNCTION                                                              */
/*============================================================================*/

int main(int argc, char* argv[]) {
    TestConfig config;
    
    // Parse command line
    if (!ParseCommandLine(argc, argv, &config)) {
        return 1;
    }
    
    // Run self-tests if requested
    if (config.self_test) {
        return RunSelfTests() ? 0 : 1;
    }
    
    printf("Advanced File Compressor - Production Tester v%s\n", PRODUCTION_TESTER_VERSION);
    printf("=================================================\n");
    
    // Create necessary directories
#ifdef _WIN32
    _mkdir("logs");
    _mkdir("restored");
    _mkdir(config.output_directory);
#else
    mkdir("logs", 0777);
    mkdir("restored", 0777);
    mkdir(config.output_directory, 0777);
#endif
    
    int result;
    
    if (config.analyze_only) {
        result = RunAnalysisOnly(&config);
    } else {
        result = RunComprehensiveTest(&config);
    }
    
    if (result == 0) {
        printf("\n🎯 SUCCESS: All operations completed successfully\n");
        printf("📊 Reports available in logs/ directory\n");
        printf("📁 Output files in %s directory\n", config.output_directory);
    } else {
        printf("\n❌ FAILURE: Some operations failed\n");
        printf("🔍 Check logs for detailed error information\n");
    }
    
    return result;
}