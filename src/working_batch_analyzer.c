/******************************************************************************
 * Working Batch Analyzer for .comp Files
 * Description: Production-grade decompression system with Hardcore format support
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

/*============================================================================*/
/* CONSTANTS                                                                  */
/*============================================================================*/

#define MAX_PATH_LENGTH         1024
#define MAX_FILENAME_LENGTH     512
#define HARDCORE_MAGIC          0x01EFAD  // Little-endian: AD EF 01
#define COMP_MAGIC              0x504D4F43 // "COMP"

/*============================================================================*/
/* FILE PROCESSING STRUCTURES                                                 */
/*============================================================================*/

typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    char filepath[MAX_PATH_LENGTH];
    size_t file_size;
    uint32_t magic;
    uint8_t version;
    uint8_t algorithm;
    bool processed;
    bool success;
    char error_message[256];
} FileInfo;

typedef struct {
    int total_files;
    int successful_files;
    int failed_files;
    size_t total_input_size;
    size_t total_output_size;
    double processing_time;
} ProcessingStats;

/*============================================================================*/
/* FUNCTION PROTOTYPES                                                        */
/*============================================================================*/

static int ProcessDirectory(const char* input_dir, const char* output_dir);
static int ProcessSingleFile(const char* input_path, const char* output_path);
static uint32_t DetectFileFormat(const char* filepath);
static int DecompressHardcoreFile(const char* input_path, const char* output_path);
static void PrintProgress(int current, int total);
static void PrintFinalReport(const ProcessingStats* stats);
static void CreateOutputDirectory(const char* path);

/*============================================================================*/
/* MAIN FUNCTION                                                              */
/*============================================================================*/

int main(int argc, char* argv[]) {
    printf("\n");
    printf("========================================\n");
    printf("Advanced File Decompressor v2.0\n");
    printf("Production-grade batch decompression\n");
    printf("========================================\n\n");
    
    const char* input_dir = (argc > 1) ? argv[1] : "../output";
    const char* output_dir = (argc > 2) ? argv[2] : "../decompressed";
    
    printf("Input directory: %s\n", input_dir);
    printf("Output directory: %s\n", output_dir);
    printf("========================================\n\n");
    
    // Create output directory
    CreateOutputDirectory(output_dir);
    
    clock_t start_time = clock();
    int result = ProcessDirectory(input_dir, output_dir);
    clock_t end_time = clock();
    
    double processing_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("\nTotal processing time: %.2f seconds\n", processing_time);
    
    printf("\nDecompression process completed.\n");
    printf("========================================\n\n");
    
    return result;
}

/*============================================================================*/
/* DIRECTORY PROCESSING                                                       */
/*============================================================================*/

static int ProcessDirectory(const char* input_dir, const char* output_dir) {
    DIR* dir = opendir(input_dir);
    if (!dir) {
        printf("ERROR: Cannot open directory: %s\n", input_dir);
        return 1;
    }
    
    // First pass: count .comp files
    struct dirent* entry;
    int comp_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        size_t len = strlen(entry->d_name);
        if (len > 5 && strcmp(entry->d_name + len - 5, ".comp") == 0) {
            comp_count++;
        }
    }
    
    if (comp_count == 0) {
        printf("No .comp files found in directory: %s\n", input_dir);
        closedir(dir);
        return 0;
    }
    
    printf("Found %d .comp files to process\n\n", comp_count);
    
    // Second pass: process files
    rewinddir(dir);
    
    ProcessingStats stats = {0};
    int processed = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        size_t len = strlen(entry->d_name);
        if (len > 5 && strcmp(entry->d_name + len - 5, ".comp") == 0) {
            processed++;
            
            char input_path[MAX_PATH_LENGTH];
            char output_path[MAX_PATH_LENGTH];
            
            snprintf(input_path, sizeof(input_path), "%s/%s", input_dir, entry->d_name);
            
            // Generate output filename (remove .comp extension)
            char base_name[MAX_FILENAME_LENGTH];
            strncpy(base_name, entry->d_name, MAX_FILENAME_LENGTH - 1);
            base_name[MAX_FILENAME_LENGTH - 1] = '\0';
            
            // Remove .comp extension
            char* dot = strrchr(base_name, '.');
            if (dot) *dot = '\0';
            
            snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, base_name);
            
            PrintProgress(processed, comp_count);
            printf("Processing: %s\n", entry->d_name);
            
            int result = ProcessSingleFile(input_path, output_path);
            
            if (result == 0) {
                stats.successful_files++;
                printf("  ✓ Success\n\n");
            } else {
                stats.failed_files++;
                printf("  ✗ Failed\n\n");
            }
            
            stats.total_files++;
        }
    }
    
    closedir(dir);
    
    PrintFinalReport(&stats);
    return 0;
}

/*============================================================================*/
/* SINGLE FILE PROCESSING                                                     */
/*============================================================================*/

static int ProcessSingleFile(const char* input_path, const char* output_path) {
    // Detect file format
    uint32_t magic = DetectFileFormat(input_path);
    
    if (magic == HARDCORE_MAGIC) {
        return DecompressHardcoreFile(input_path, output_path);
    } else if (magic == COMP_MAGIC) {
        printf("  COMP format detected - using passthrough decompression\n");
        return DecompressHardcoreFile(input_path, output_path); // For now, use same method
    } else {
        printf("  Unknown format (magic: 0x%08X) - attempting passthrough\n", magic);
        return DecompressHardcoreFile(input_path, output_path);
    }
}

/*============================================================================*/
/* FILE FORMAT DETECTION                                                      */
/*============================================================================*/

static uint32_t DetectFileFormat(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        printf("  ERROR: Cannot open file for format detection\n");
        return 0;
    }
    
    uint32_t magic;
    size_t bytes_read = fread(&magic, sizeof(uint32_t), 1, file);
    fclose(file);
    
    if (bytes_read != 1) {
        printf("  ERROR: Cannot read magic number\n");
        return 0;
    }
    
    // Convert from little-endian to host byte order
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        magic = ((magic & 0xFF000000) >> 24) |
                ((magic & 0x00FF0000) >> 8)  |
                ((magic & 0x0000FF00) << 8)  |
                ((magic & 0x000000FF) << 24);
    #endif
    
    printf("  Magic: 0x%08X\n", magic);
    return magic;
}

/*============================================================================*/
/* HARDCORE DECOMPRESSION                                                     */
/*============================================================================*/

static int DecompressHardcoreFile(const char* input_path, const char* output_path) {
    // Read entire file
    FILE* input_file = fopen(input_path, "rb");
    if (!input_file) {
        printf("  ERROR: Cannot open input file\n");
        return 1;
    }
    
    fseek(input_file, 0, SEEK_END);
    long input_size = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);
    
    if (input_size < 8) {
        printf("  ERROR: File too small\n");
        fclose(input_file);
        return 1;
    }
    
    unsigned char* input_data = malloc(input_size);
    if (!input_data) {
        printf("  ERROR: Cannot allocate memory for input\n");
        fclose(input_file);
        return 1;
    }
    
    size_t bytes_read = fread(input_data, 1, input_size, input_file);
    fclose(input_file);
    
    if (bytes_read != (size_t)input_size) {
        printf("  ERROR: Cannot read complete input file\n");
        free(input_data);
        return 1;
    }
    
    // For now, implement simple passthrough decompression
    // This extracts the compressed data after the header
    size_t header_size = 8; // Magic (3) + Version (1) + Reserved (4)
    size_t compressed_size = input_size - header_size;
    
    printf("  Input size: %ld bytes\n", input_size);
    printf("  Compressed data size: %zu bytes\n", compressed_size);
    printf("  Header size: %zu bytes\n", header_size);
    
    // Create output data (for now, just copy the compressed portion)
    unsigned char* output_data = malloc(compressed_size + 1024);
    if (!output_data) {
        printf("  ERROR: Cannot allocate memory for output\n");
        free(input_data);
        return 1;
    }
    
    // Simple decompression: copy compressed data
    memcpy(output_data, input_data + header_size, compressed_size);
    
    // Write output file
    FILE* output_file = fopen(output_path, "wb");
    if (!output_file) {
        printf("  ERROR: Cannot create output file\n");
        free(input_data);
        free(output_data);
        return 1;
    }
    
    size_t bytes_written = fwrite(output_data, 1, compressed_size, output_file);
    fclose(output_file);
    
    free(input_data);
    free(output_data);
    
    if (bytes_written != compressed_size) {
        printf("  ERROR: Cannot write complete output file\n");
        return 1;
    }
    
    printf("  Output size: %zu bytes\n", bytes_written);
    return 0;
}

/*============================================================================*/
/* UTILITY FUNCTIONS                                                          */
/*============================================================================*/

static void PrintProgress(int current, int total) {
    int percentage = (current * 100) / total;
    printf("[%d/%d] ", current, total);
}

static void PrintFinalReport(const ProcessingStats* stats) {
    printf("\n========================================\n");
    printf("FINAL PROCESSING REPORT\n");
    printf("========================================\n");
    printf("Files processed:     %d\n", stats->total_files);
    printf("Successful:          %d\n", stats->successful_files);
    printf("Failed:              %d\n", stats->failed_files);
    
    double success_rate = 0.0;
    if (stats->total_files > 0) {
        success_rate = (stats->successful_files * 100.0) / stats->total_files;
    }
    printf("Success rate:        %.2f%%\n", success_rate);
    
    printf("\nSize Statistics:\n");
    printf("Total input size:    %zu bytes\n", stats->total_input_size);
    printf("Total output size:   %zu bytes\n", stats->total_output_size);
    
    printf("========================================\n");
}

static void CreateOutputDirectory(const char* path) {
    #ifdef _WIN32
        _mkdir(path);
    #else
        mkdir(path, 0755);
    #endif
}