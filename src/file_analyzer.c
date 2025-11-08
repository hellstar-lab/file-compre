/******************************************************************************
 * Advanced File Compressor - File Analyzer Module
 * Description: Comprehensive analysis tool for .comp files with header validation
 ******************************************************************************/

#include "../include/decompressor.h"
#include <string.h>
#include <ctype.h>

/*============================================================================*/
/* CONSTANTS                                                                  */
/*============================================================================*/

#define ANALYZER_BUFFER_SIZE    1024
#define MAX_ANALYSIS_FILES      100
#define COMP_MAGIC_V3           0x504D4F43  // "COMP" in little-endian
#define COMP_MAGIC_V4           0x504D4F43  // "COMP" in little-endian
#define HARDCORE_MAGIC          0x01EFAD    // Hardcore magic number

/*============================================================================*/
/* STRUCTURES                                                                 */
/*============================================================================*/

typedef struct {
    char filename[DECOMP_MAX_FILENAME];
    char filepath[DECOMP_MAX_PATH];
    uint64_t file_size;
    uint32_t magic_number;
    uint8_t version;
    uint8_t algorithm;
    uint8_t level;
    uint64_t original_size;
    uint64_t compressed_size;
    uint32_t checksum;
    uint32_t block_count;
    bool is_valid_header;
    bool is_hardcore_container;
    bool has_block_table;
    char algorithm_name[32];
    char status[64];
    char error_details[256];
} FileAnalysis;

typedef struct {
    FileAnalysis* files;
    int file_count;
    int valid_files;
    int invalid_files;
    int hardcore_files;
    int blockwise_files;
    char report_path[DECOMP_MAX_PATH];
} AnalysisContext;

/*============================================================================*/
/* PRIVATE FUNCTIONS                                                          */
/*============================================================================*/

static const char* GetAlgorithmName(uint8_t algorithm) {
    switch (algorithm) {
        case 0: return "Huffman";
        case 1: return "LZ77";
        case 2: return "LZW";
        case 3: return "RLE";
        case 4: return "BWT+MTF+Huffman";
        case 5: return "Hardcore/LZMA";
        case 6: return "Audio Advanced";
        case 7: return "Image Advanced";
        case 255: return "Blockwise";
        default: return "Unknown";
    }
}

static bool AnalyzeCompHeader(FILE* file, FileAnalysis* analysis) {
    if (!file || !analysis) return false;
    
    // Read magic number
    uint32_t magic;
    if (fread(&magic, sizeof(uint32_t), 1, file) != 1) {
        strcpy(analysis->error_details, "Failed to read magic number");
        return false;
    }
    
    analysis->magic_number = magic;
    
    // Check for COMP magic
    if (magic == COMP_MAGIC_V3 || magic == COMP_MAGIC_V4) {
        // Read version
        if (fread(&analysis->version, sizeof(uint8_t), 1, file) != 1) {
            strcpy(analysis->error_details, "Failed to read version");
            return false;
        }
        
        if (analysis->version == 3) {
            // V3 header format
            if (fread(&analysis->algorithm, sizeof(uint8_t), 1, file) != 1) {
                strcpy(analysis->error_details, "Failed to read algorithm");
                return false;
            }
            
            if (fread(&analysis->original_size, sizeof(uint64_t), 1, file) != 1) {
                strcpy(analysis->error_details, "Failed to read original size");
                return false;
            }
            
            if (fread(&analysis->compressed_size, sizeof(uint64_t), 1, file) != 1) {
                strcpy(analysis->error_details, "Failed to read compressed size");
                return false;
            }
            
            if (fread(&analysis->checksum, sizeof(uint32_t), 1, file) != 1) {
                strcpy(analysis->error_details, "Failed to read checksum");
                return false;
            }
            
            analysis->level = 0; // Not specified in V3
            analysis->block_count = 1; // Single block
            analysis->has_block_table = false;
            
        } else if (analysis->version == 4) {
            // V4 header format
            if (fread(&analysis->algorithm, sizeof(uint8_t), 1, file) != 1) {
                strcpy(analysis->error_details, "Failed to read algorithm");
                return false;
            }
            
            if (fread(&analysis->level, sizeof(uint8_t), 1, file) != 1) {
                strcpy(analysis->error_details, "Failed to read level");
                return false;
            }
            
            // Skip file type byte
            fseek(file, 1, SEEK_CUR);
            
            // Read 8-byte big-endian original size
            uint64_t orig_size_be;
            if (fread(&orig_size_be, sizeof(uint64_t), 1, file) != 1) {
                strcpy(analysis->error_details, "Failed to read original size");
                return false;
            }
            analysis->original_size = be64toh(orig_size_be);
            
            // Read 8-byte big-endian compressed total
            uint64_t comp_size_be;
            if (fread(&comp_size_be, sizeof(uint64_t), 1, file) != 1) {
                strcpy(analysis->error_details, "Failed to read compressed size");
                return false;
            }
            analysis->compressed_size = be64toh(comp_size_be);
            
            // Read 4-byte big-endian block count
            uint32_t block_count_be;
            if (fread(&block_count_be, sizeof(uint32_t), 1, file) != 1) {
                strcpy(analysis->error_details, "Failed to read block count");
                return false;
            }
            analysis->block_count = be32toh(block_count_be);
            
            // Check for BLK4 marker
            char blk_marker[4];
            if (fread(blk_marker, 4, 1, file) == 1) {
                if (memcmp(blk_marker, "BLK4", 4) == 0) {
                    analysis->has_block_table = true;
                    if (analysis->algorithm == 255) {
                        analysis->is_hardcore_container = false; // It's blockwise
                    }
                }
            }
            
            analysis->checksum = 0; // Not in V4 header
            
        } else {
            snprintf(analysis->error_details, sizeof(analysis->error_details), 
                     "Unsupported version: %d", analysis->version);
            return false;
        }
        
        strcpy(analysis->algorithm_name, GetAlgorithmName(analysis->algorithm));
        analysis->is_valid_header = true;
        strcpy(analysis->status, "Valid COMP header");
        return true;
        
    } else {
        // Check for Hardcore container magic
        fseek(file, 0, SEEK_SET);
        uint8_t hardcore_magic[3];
        if (fread(hardcore_magic, 3, 1, file) == 1) {
            if (hardcore_magic[0] == 0xAD && hardcore_magic[1] == 0xEF && hardcore_magic[2] == 0x01) {
                analysis->is_hardcore_container = true;
                analysis->version = 0; // Hardcore version
                analysis->algorithm = 5; // Hardcore/LZMA
                strcpy(analysis->algorithm_name, "Hardcore Container");
                
                // Try to read original size from offset 4
                fseek(file, 4, SEEK_SET);
                uint32_t orig_size_le;
                if (fread(&orig_size_le, sizeof(uint32_t), 1, file) == 1) {
                    analysis->original_size = orig_size_le;
                }
                
                analysis->compressed_size = analysis->file_size - 8; // Approximate
                analysis->is_valid_header = true;
                strcpy(analysis->status, "Valid Hardcore container");
                return true;
            }
        }
        
        snprintf(analysis->error_details, sizeof(analysis->error_details), 
                 "Invalid magic number: 0x%08X", magic);
        strcpy(analysis->status, "Invalid header");
        return false;
    }
}

static bool AnalyzeSingleFile(const char* filepath, FileAnalysis* analysis) {
    if (!filepath || !analysis) return false;
    
    // Initialize analysis structure
    memset(analysis, 0, sizeof(FileAnalysis));
    
    // Extract filename
    const char* filename = strrchr(filepath, '\\');
    if (!filename) filename = strrchr(filepath, '/');
    if (!filename) filename = filepath;
    else filename++; // Skip the separator
    
    strncpy(analysis->filename, filename, sizeof(analysis->filename) - 1);
    strncpy(analysis->filepath, filepath, sizeof(analysis->filepath) - 1);
    
    // Get file size
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        strcpy(analysis->error_details, "Failed to open file");
        strcpy(analysis->status, "File access error");
        return false;
    }
    
    fseek(file, 0, SEEK_END);
    analysis->file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (analysis->file_size < 16) {
        strcpy(analysis->error_details, "File too small for valid header");
        strcpy(analysis->status, "Invalid file size");
        fclose(file);
        return false;
    }
    
    // Analyze header
    bool result = AnalyzeCompHeader(file, analysis);
    fclose(file);
    
    return result;
}

static void GenerateAnalysisReport(const AnalysisContext* context) {
    if (!context) return;
    
    FILE* report = fopen(context->report_path, "w");
    if (!report) {
        printf("Error: Failed to create analysis report: %s\n", context->report_path);
        return;
    }
    
    fprintf(report, "Advanced File Compressor - File Analysis Report\n");
    fprintf(report, "===============================================\n\n");
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(report, "Analysis Date: %s\n\n", timestamp);
    
    fprintf(report, "Summary:\n");
    fprintf(report, "--------\n");
    fprintf(report, "Total Files Analyzed: %d\n", context->file_count);
    fprintf(report, "Valid Headers: %d\n", context->valid_files);
    fprintf(report, "Invalid Headers: %d\n", context->invalid_files);
    fprintf(report, "Hardcore Containers: %d\n", context->hardcore_files);
    fprintf(report, "Blockwise Files: %d\n", context->blockwise_files);
    fprintf(report, "\nDetailed Analysis:\n");
    fprintf(report, "==================\n");
    
    fprintf(report, "%-40s %-8s %-3s %-15s %-3s %-12s %-12s %-6s %-20s\n",
            "Filename", "Version", "Alg", "Algorithm", "Lvl", "Original", "Compressed", "Blocks", "Status");
    fprintf(report, "%s\n", "--------------------------------------------------------------------------------------------------------");
    
    for (int i = 0; i < context->file_count; i++) {
        const FileAnalysis* file = &context->files[i];
        
        fprintf(report, "%-40s %-8d %-3d %-15s %-3d %-12llu %-12llu %-6u %-20s\n",
                file->filename,
                file->version,
                file->algorithm,
                file->algorithm_name,
                file->level,
                file->original_size,
                file->compressed_size,
                file->block_count,
                file->status);
        
        if (strlen(file->error_details) > 0) {
            fprintf(report, "    Error: %s\n", file->error_details);
        }
    }
    
    fprintf(report, "\nRecommendations:\n");
    fprintf(report, "================\n");
    
    if (context->invalid_files > 0) {
        fprintf(report, "- %d files have invalid headers and may need repair\n", context->invalid_files);
    }
    
    if (context->hardcore_files > 0) {
        fprintf(report, "- %d files use Hardcore/LZMA compression and may need special handling\n", context->hardcore_files);
    }
    
    if (context->blockwise_files > 0) {
        fprintf(report, "- %d files use blockwise compression with multiple blocks\n", context->blockwise_files);
    }
    
    fprintf(report, "- Use universal_decompressor with -r flag for auto-repair mode\n");
    fprintf(report, "- Use -i flag for integrity verification\n");
    fprintf(report, "- Check logs/hardcore_debug/ for LZMA decoding details\n");
    
    fclose(report);
    printf("[INFO] Analysis report generated: %s\n", context->report_path);
}

/*============================================================================*/
/* PUBLIC FUNCTIONS                                                           */
/*============================================================================*/

int FileAnalyzer_AnalyzeDirectory(const char* directory, const char* report_path) {
    if (!directory) {
        printf("Error: Invalid directory path\n");
        return -1;
    }
    
    printf("[INFO] Starting file analysis for directory: %s\n", directory);
    
    AnalysisContext context = {0};
    context.files = (FileAnalysis*)malloc(MAX_ANALYSIS_FILES * sizeof(FileAnalysis));
    if (!context.files) {
        printf("Error: Memory allocation failed\n");
        return -1;
    }
    
    if (report_path) {
        strncpy(context.report_path, report_path, sizeof(context.report_path) - 1);
    } else {
        snprintf(context.report_path, sizeof(context.report_path), 
                 "%s/file_analysis_report.txt", directory);
    }
    
    // Scan directory for .comp files
#ifdef _WIN32
    char search_pattern[DECOMP_MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.comp", directory);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFile(search_pattern, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        printf("Warning: No .comp files found in directory: %s\n", directory);
        free(context.files);
        return 0;
    }
    
    do {
        if (context.file_count >= MAX_ANALYSIS_FILES) {
            printf("Warning: Maximum file limit reached: %d\n", MAX_ANALYSIS_FILES);
            break;
        }
        
        char filepath[DECOMP_MAX_PATH];
        snprintf(filepath, sizeof(filepath), "%s\\%s", directory, find_data.cFileName);
        
        FileAnalysis* analysis = &context.files[context.file_count];
        if (AnalyzeSingleFile(filepath, analysis)) {
            context.valid_files++;
            if (analysis->is_hardcore_container) {
                context.hardcore_files++;
            }
            if (analysis->has_block_table && analysis->block_count > 1) {
                context.blockwise_files++;
            }
        } else {
            context.invalid_files++;
        }
        
        context.file_count++;
        
        printf("[INFO] Analyzed: %s - %s\n", find_data.cFileName, analysis->status);
        
    } while (FindNextFile(find_handle, &find_data));
    
    FindClose(find_handle);
    
#else
    // Unix/Linux implementation
    DIR* dir = opendir(directory);
    if (!dir) {
        printf("Error: Failed to open directory: %s\n", directory);
        free(context.files);
        return -1;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && context.file_count < MAX_ANALYSIS_FILES) {
        // Check if file has .comp extension
        size_t len = strlen(entry->d_name);
        if (len < 5 || strcasecmp(entry->d_name + len - 5, ".comp") != 0) {
            continue;
        }
        
        char filepath[DECOMP_MAX_PATH];
        snprintf(filepath, sizeof(filepath), "%s/%s", directory, entry->d_name);
        
        FileAnalysis* analysis = &context.files[context.file_count];
        if (AnalyzeSingleFile(filepath, analysis)) {
            context.valid_files++;
            if (analysis->is_hardcore_container) {
                context.hardcore_files++;
            }
            if (analysis->has_block_table && analysis->block_count > 1) {
                context.blockwise_files++;
            }
        } else {
            context.invalid_files++;
        }
        
        context.file_count++;
        
        printf("[INFO] Analyzed: %s - %s\n", entry->d_name, analysis->status);
    }
    
    closedir(dir);
#endif
    
    // Generate report
    GenerateAnalysisReport(&context);
    
    printf("\n[SUMMARY] Analysis completed:\n");
    printf("  Total files: %d\n", context.file_count);
    printf("  Valid headers: %d\n", context.valid_files);
    printf("  Invalid headers: %d\n", context.invalid_files);
    printf("  Hardcore containers: %d\n", context.hardcore_files);
    printf("  Blockwise files: %d\n", context.blockwise_files);
    
    free(context.files);
    return context.file_count;
}

int FileAnalyzer_AnalyzeSingle(const char* filepath) {
    if (!filepath) {
        printf("Error: Invalid file path\n");
        return -1;
    }
    
    FileAnalysis analysis;
    if (AnalyzeSingleFile(filepath, &analysis)) {
        printf("\n[FILE ANALYSIS] %s\n", analysis.filename);
        printf("  Status: %s\n", analysis.status);
        printf("  Version: %d\n", analysis.version);
        printf("  Algorithm: %d (%s)\n", analysis.algorithm, analysis.algorithm_name);
        printf("  Level: %d\n", analysis.level);
        printf("  Original Size: %llu bytes\n", analysis.original_size);
        printf("  Compressed Size: %llu bytes\n", analysis.compressed_size);
        printf("  Block Count: %u\n", analysis.block_count);
        printf("  File Size: %llu bytes\n", analysis.file_size);
        
        if (analysis.is_hardcore_container) {
            printf("  Type: Hardcore Container\n");
        } else if (analysis.has_block_table) {
            printf("  Type: Blockwise COMP\n");
        } else {
            printf("  Type: Single-block COMP\n");
        }
        
        if (strlen(analysis.error_details) > 0) {
            printf("  Error: %s\n", analysis.error_details);
        }
        
        return 1;
    } else {
        printf("\n[FILE ANALYSIS] %s\n", analysis.filename);
        printf("  Status: %s\n", analysis.status);
        printf("  Error: %s\n", analysis.error_details);
        return 0;
    }
}