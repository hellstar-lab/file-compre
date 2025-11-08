#include "../include/compressor.h"
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#endif

// Analyze file content to determine characteristics
typedef struct {
    double text_ratio;      // Ratio of printable text characters
    double entropy;         // Shannon entropy
    int has_binary_header;  // Has known binary file signatures
    int repetition_factor;  // Amount of repetitive patterns
} ContentAnalysis;

// Calculate Shannon entropy of data
double calculate_entropy(const unsigned char* data, size_t size) {
    if (size == 0) return 0.0;
    
    int freq[256] = {0};
    for (size_t i = 0; i < size; i++) {
        freq[data[i]]++;
    }
    
    double entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] / size;
            entropy -= p * log2(p);
        }
    }
    
    return entropy;
}

// Analyze file content characteristics
ContentAnalysis analyze_file_content(const unsigned char* data, size_t size) {
    ContentAnalysis analysis = {0};
    
    if (size == 0) return analysis;
    
    // Calculate text ratio
    int printable_count = 0;
    int whitespace_count = 0;
    for (size_t i = 0; i < size && i < 8192; i++) { // Sample first 8KB
        if (isprint(data[i]) || isspace(data[i])) {
            printable_count++;
            if (isspace(data[i])) whitespace_count++;
        }
    }
    
    size_t sample_size = (size < 8192) ? size : 8192;
    analysis.text_ratio = (double)printable_count / sample_size;
    
    // Calculate entropy
    analysis.entropy = calculate_entropy(data, (size < 4096) ? size : 4096);
    
    // Check for binary file signatures
    if (size >= 4) {
        // PDF signature
        if (memcmp(data, "%PDF", 4) == 0) {
            analysis.has_binary_header = 1;
        }
        // PNG signature
        else if (size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
            analysis.has_binary_header = 1;
        }
        // JPEG signature
        else if (memcmp(data, "\xFF\xD8\xFF", 3) == 0) {
            analysis.has_binary_header = 1;
        }
        // WAV signature
        else if (memcmp(data, "RIFF", 4) == 0 && size >= 12 && 
                 memcmp(data + 8, "WAVE", 4) == 0) {
            analysis.has_binary_header = 1;
        }
        // ZIP/compressed file signatures
        else if (memcmp(data, "PK", 2) == 0) {
            analysis.has_binary_header = 1;
        }
    }
    
    // Calculate repetition factor (simple pattern detection)
    int repetitive_bytes = 0;
    for (size_t i = 1; i < sample_size; i++) {
        if (data[i] == data[i-1]) repetitive_bytes++;
    }
    analysis.repetition_factor = (repetitive_bytes * 100) / sample_size;
    
    return analysis;
}

// Enhanced file type detection with content analysis
FileType detect_file_type_enhanced(const char* filename, const unsigned char* data, size_t size) {
    FileType ext_type = detect_file_type(filename);
    
    // If we have a clear extension match and it's reliable, use it
    if (ext_type != FILE_TYPE_UNKNOWN) {
        return ext_type;
    }
    
    // Perform content analysis for unknown extensions
    if (data && size > 0) {
        // Strong magic checks first for image formats
        if (size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
            return FILE_TYPE_PNG;
        }
        if (size >= 54 && memcmp(data, "BM", 2) == 0 && (*(const unsigned int*)(data+14) == 40) && (*(const unsigned short*)(data+28) == 24)) {
            return FILE_TYPE_BMP;
        }
        if ((size >= 18 && data[2] == 2 && data[16] == 24) ||
            (size >= 26 && memcmp(data + size - 18, "TRUEVISION-XFILE", 16) == 0)) {
            return FILE_TYPE_TGA;
        }
        ContentAnalysis analysis = analyze_file_content(data, size);
        
        // Binary file with known signature
        if (analysis.has_binary_header) {
            // Detect common container signatures
            if (size >= 4 && memcmp(data, "PK\x03\x04", 4) == 0) {
                // Likely ZIP container (DOCX/XLSX/PPTX)
                return FILE_TYPE_DOCX;
            }
            if (size >= 4 && memcmp(data, "%PDF", 4) == 0) {
                return FILE_TYPE_PDF;
            }
            return FILE_TYPE_IMAGE; // Generic binary container
        }
        
        // High text ratio suggests text file
        if (analysis.text_ratio > 0.95) {
            return FILE_TYPE_TEXT;
        }
        
        // Medium text ratio with structured content might be XML/JSON
        if (analysis.text_ratio > 0.8) {
            // Quick check for JSON/XML patterns
            if (data[0] == '{' || data[0] == '[') {
                return FILE_TYPE_JSON;
            }
            if (data[0] == '<') {
                return FILE_TYPE_XML;
            }
            return FILE_TYPE_TEXT;
        }
        
        // Low entropy suggests highly repetitive data (good for compression)
        if (analysis.entropy < 3.0) {
            return FILE_TYPE_TEXT; // Treat as text-like for better compression
        }
        
        // High entropy suggests already compressed or encrypted data
        if (analysis.entropy > 7.5) {
            return FILE_TYPE_IMAGE; // Treat as binary/image type
        }
    }
    
    return FILE_TYPE_UNKNOWN;
}

// Original detect_file_type function (kept for backward compatibility)
FileType detect_file_type(const char* filename) {
    if (!filename) return FILE_TYPE_UNKNOWN;
    
    // Find the last dot in filename
    const char* ext = strrchr(filename, '.');
    if (!ext) return FILE_TYPE_UNKNOWN;
    
    ext++; // Skip the dot
    
    // Convert to lowercase for comparison
    char lower_ext[10];
    int i;
    for (i = 0; i < 9 && ext[i]; i++) {
        lower_ext[i] = tolower(ext[i]);
    }
    lower_ext[i] = '\0';
    
    // Check file types
    if (strcmp(lower_ext, "txt") == 0 || strcmp(lower_ext, "log") == 0 || 
        strcmp(lower_ext, "md") == 0 || strcmp(lower_ext, "readme") == 0) return FILE_TYPE_TEXT;
    if (strcmp(lower_ext, "pdf") == 0) return FILE_TYPE_PDF;
    if (strcmp(lower_ext, "docx") == 0 || strcmp(lower_ext, "xlsx") == 0 || strcmp(lower_ext, "pptx") == 0) return FILE_TYPE_DOCX;
    if (strcmp(lower_ext, "xml") == 0 || strcmp(lower_ext, "html") == 0 || 
        strcmp(lower_ext, "htm") == 0 || strcmp(lower_ext, "xhtml") == 0) return FILE_TYPE_XML;
    if (strcmp(lower_ext, "wav") == 0 || strcmp(lower_ext, "mp3") == 0 || 
        strcmp(lower_ext, "flac") == 0 || strcmp(lower_ext, "aac") == 0 ||
        strcmp(lower_ext, "ogg") == 0) return FILE_TYPE_AUDIO;
    if (strcmp(lower_ext, "csv") == 0 || strcmp(lower_ext, "tsv") == 0) return FILE_TYPE_CSV;
    if (strcmp(lower_ext, "json") == 0 || strcmp(lower_ext, "js") == 0 ||
        strcmp(lower_ext, "jsonl") == 0) return FILE_TYPE_JSON;
    if (strcmp(lower_ext, "bmp") == 0) return FILE_TYPE_BMP;
    if (strcmp(lower_ext, "png") == 0) return FILE_TYPE_PNG;
    if (strcmp(lower_ext, "tga") == 0) return FILE_TYPE_TGA;
    if (strcmp(lower_ext, "jpg") == 0 || strcmp(lower_ext, "jpeg") == 0 ||
        strcmp(lower_ext, "tiff") == 0 || strcmp(lower_ext, "gif") == 0 ||
        strcmp(lower_ext, "webp") == 0 || strcmp(lower_ext, "svg") == 0) return FILE_TYPE_IMAGE;
    
    return FILE_TYPE_UNKNOWN;
}

// Read entire file into memory
int read_file(const char* filepath, unsigned char** buffer, long* size) {
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        printf("Error: Cannot open file '%s' for reading.\n", filepath);
        return -1;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (*size <= 0) {
        printf("Error: File '%s' is empty or invalid.\n", filepath);
        fclose(file);
        return -1;
    }
    
    // Allocate buffer
    *buffer = (unsigned char*)malloc(*size);
    if (!*buffer) {
        printf("Error: Cannot allocate memory for file '%s'.\n", filepath);
        fclose(file);
        return -1;
    }
    
    // Read file
    size_t bytes_read = fread(*buffer, 1, *size, file);
    fclose(file);
    
    if (bytes_read != *size) {
        printf("Error: Could not read entire file '%s'.\n", filepath);
        free(*buffer);
        return -1;
    }
    
    return 0;
}

// Write buffer to file
int write_file(const char* filepath, const unsigned char* buffer, long size) {
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        printf("Error: Cannot open file '%s' for writing.\n", filepath);
        return -1;
    }
    
    size_t bytes_written = fwrite(buffer, 1, size, file);
    fclose(file);
    
    if (bytes_written != size) {
        printf("Error: Could not write entire buffer to file '%s'.\n", filepath);
        return -1;
    }
    
    return 0;
}

// Copy file to data folder
int copy_file_to_data_folder(const char* source_path, char* dest_path) {
    // Extract filename from source path
    const char* filename = strrchr(source_path, '\\');
    if (!filename) {
        filename = strrchr(source_path, '/');
    }
    if (!filename) {
        filename = source_path;
    } else {
        filename++; // Skip the slash
    }
    
    // Create destination path
    snprintf(dest_path, MAX_PATH_LENGTH, "data\\%s", filename);
    
    // Read source file
    unsigned char* buffer;
    long size;
    if (read_file(source_path, &buffer, &size) != 0) {
        return -1;
    }
    
    // Write to destination
    int result = write_file(dest_path, buffer, size);
    free(buffer);
    
    if (result == 0) {
        printf("File copied to: %s\n", dest_path);
    }
    
    return result;
}

// Get file extension
char* get_file_extension(const char* filename) {
    char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) return NULL;
    return dot + 1;
}

// Generate compressed filename
void generate_compressed_filename(const char* original_filename, char* compressed_filename) {
    // Extract base name without extension
    char base_name[MAX_FILENAME_LENGTH];
    strcpy(base_name, original_filename);
    
    char* dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';
    
    // Extract just the filename from path
    char* filename_only = strrchr(base_name, '\\');
    if (!filename_only) {
        filename_only = strrchr(base_name, '/');
    }
    if (filename_only) {
        filename_only++;
    } else {
        filename_only = base_name;
    }
    
    snprintf(compressed_filename, MAX_PATH_LENGTH, "output\\%s.comp", filename_only);
}

// Generate decompressed filename
void generate_decompressed_filename(const char* compressed_filename, char* decompressed_filename) {
    // Extract base name without .comp extension
    char base_name[MAX_FILENAME_LENGTH];
    strcpy(base_name, compressed_filename);
    
    char* comp_ext = strstr(base_name, ".comp");
    if (comp_ext) *comp_ext = '\0';
    
    // Extract just the filename from path
    char* filename_only = strrchr(base_name, '\\');
    if (!filename_only) {
        filename_only = strrchr(base_name, '/');
    }
    if (filename_only) {
        filename_only++;
    } else {
        filename_only = base_name;
    }
    
    snprintf(decompressed_filename, MAX_PATH_LENGTH, "output\\%s_decompressed.txt", filename_only);
}

// Generate decompressed filename with original extension
void generate_decompressed_filename_with_ext(const char* compressed_filename, const char* original_extension, char* decompressed_filename) {
    // Extract base name without .comp extension
    char base_name[MAX_FILENAME_LENGTH];
    strcpy(base_name, compressed_filename);
    
    char* comp_ext = strstr(base_name, ".comp");
    if (comp_ext) *comp_ext = '\0';
    
    // Extract just the filename from path
    char* filename_only = strrchr(base_name, '\\');
    if (!filename_only) {
        filename_only = strrchr(base_name, '/');
    }
    if (filename_only) {
        filename_only++;
    } else {
        filename_only = base_name;
    }
    
    // Use original extension if available, otherwise default to .txt
    if (original_extension && strlen(original_extension) > 0) {
        snprintf(decompressed_filename, MAX_PATH_LENGTH, "output\\%s_decompressed.%s", filename_only, original_extension);
    } else {
        snprintf(decompressed_filename, MAX_PATH_LENGTH, "output\\%s_decompressed.txt", filename_only);
    }
}

// Print main menu
void print_menu() {
    printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    printf("           PROFESSIONAL FILE COMPRESSOR v2.0\n");
    printf("" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    printf("1. Upload and Compress File\n");
    printf("2. Decompress File\n");
    printf("3. List Files in Data Folder\n");
    printf("4. List Files in Output Folder\n");
    printf("5. Performance Benchmark\n");
    printf("0. Exit\n");
    printf("" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    printf("Enter your choice: ");
}

// Get user choice
int get_user_choice() {
    int choice;
    if (scanf("%d", &choice) != 1) {
        choice = -1;
    }
    getchar(); // Consume newline
    return choice;
}

// Print compression statistics
void print_compression_stats(const CompressionStats* stats) {
    printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    printf("           COMPRESSION STATISTICS\n");
    printf("" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    printf("Original File: %s\n", stats->original_filename);
    printf("Compressed File: %s\n", stats->compressed_filename);
    printf("Original Size: %ld bytes (%.2f MB)\n", stats->original_size, stats->original_size / 1024.0 / 1024.0);
    printf("Compressed Size: %ld bytes (%.2f MB)\n", stats->compressed_size, stats->compressed_size / 1024.0 / 1024.0);
    printf("Compression Ratio: %.2f%%\n", stats->compression_ratio);
    printf("Space Saved: %ld bytes (%.2f%%)\n", 
           stats->original_size - stats->compressed_size,
           100.0 - stats->compression_ratio);
    
    const char* algo_name;
    switch (stats->algorithm_used) {
        case ALGO_HUFFMAN: algo_name = "Huffman Coding"; break;
        case ALGO_LZ77: algo_name = "LZ77"; break;
        case ALGO_LZW: algo_name = "LZW"; break;
        case ALGO_AUDIO_ADVANCED: algo_name = "Advanced Audio"; break;
        case ALGO_IMAGE_ADVANCED: algo_name = "Advanced Image"; break;
        default: algo_name = "Unknown"; break;
    }
    printf("Algorithm Used: %s\n", algo_name);
    
    const char* level_name;
    switch (stats->compression_level) {
        case COMPRESSION_LEVEL_FAST: level_name = "Fast"; break;
        case COMPRESSION_LEVEL_NORMAL: level_name = "Normal"; break;
        case COMPRESSION_LEVEL_HIGH: level_name = "High"; break;
        case COMPRESSION_LEVEL_ULTRA: level_name = "Ultra"; break;
        default: level_name = "Unknown"; break;
    }
    printf("Compression Level: %s\n", level_name);
    
    if (stats->compression_speed > 0) {
        printf("Compression Speed: %.2f MB/s\n", stats->compression_speed);
    }
    
    if (stats->memory_usage > 0) {
        printf("Peak Memory Usage: %.2f MB\n", stats->memory_usage);
    }
    
    char time_str[100];
    struct tm* time_info = localtime(&stats->compression_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);
    printf("Compression Time: %s\n", time_str);
    printf("" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
}

// Optimized file writing with buffered I/O
int write_file_optimized(const char* filepath, const unsigned char* buffer, long size) {
    if (!filepath || !buffer || size <= 0) {
        return -1;
    }
    
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        printf("Error: Cannot create file '%s'.\n", filepath);
        return -1;
    }
    
    // Use larger buffer for better I/O performance
    const size_t FILE_BUFFER_SIZE = 65536; // 64KB buffer
    if (setvbuf(file, NULL, _IOFBF, FILE_BUFFER_SIZE) != 0) {
        // If setvbuf fails, continue with default buffering
    }
    
    // Write data in chunks for better performance
    const size_t CHUNK_SIZE = 32768; // 32KB chunks
    long bytes_written = 0;
    
    while (bytes_written < size) {
        size_t chunk_size = (size - bytes_written > CHUNK_SIZE) ? 
                           CHUNK_SIZE : (size - bytes_written);
        
        size_t written = fwrite(buffer + bytes_written, 1, chunk_size, file);
        if (written != chunk_size) {
            printf("Error: Failed to write data to file.\n");
            fclose(file);
            return -1;
        }
        
        bytes_written += written;
    }
    
    // Ensure data is flushed to disk
    if (fflush(file) != 0) {
        printf("Error: Failed to flush data to file.\n");
        fclose(file);
        return -1;
    }
    
    fclose(file);
    return 0;
}
int list_files_in_directory(const char* directory_path) {
    struct _finddata_t file_info;
    intptr_t handle;
    char search_path[MAX_PATH_LENGTH];
    int file_count = 0;
    
    snprintf(search_path, sizeof(search_path), "%s\\*.*", directory_path);
    
    handle = _findfirst(search_path, &file_info);
    if (handle == -1) {
        printf("No files found in directory '%s' or directory doesn't exist.\n", directory_path);
        return 0;
    }
    
    printf("\nFiles in '%s':\n", directory_path);
    printf("" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "\n");
    
    do {
        if (!(file_info.attrib & _A_SUBDIR)) { // Not a directory
            printf("%d. %s (%ld bytes)\n", ++file_count, file_info.name, file_info.size);
        }
    } while (_findnext(handle, &file_info) == 0);
    
    _findclose(handle);
    
    if (file_count == 0) {
        printf("No files found.\n");
    } else {
        printf("" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "-" "\n");
        printf("Total: %d files\n", file_count);
    }
    
    return file_count;
}

// Delete file
int delete_file(const char* filepath) {
    if (remove(filepath) == 0) {
        return 0;
    } else {
        printf("Error: Could not delete file '%s'.\n", filepath);
        return -1;
    }
}