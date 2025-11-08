/******************************************************************************
 * Advanced File Compressor - File I/O Module (Fixed)
 * Description: Robust file input/output operations with error handling
 ******************************************************************************/

#include "../include/decompressor.h"
#include <sys/stat.h>
#include <errno.h>

/*============================================================================*/
/* PRIVATE CONSTANTS                                                          */
/*============================================================================*/

#define FILE_IO_BUFFER_SIZE     65536
#define MAX_PATH_LENGTH         1024
#define MAX_FILENAME_LENGTH     512

/*============================================================================*/
/* PRIVATE STRUCTURES                                                         */
/*============================================================================*/

typedef struct {
    char input_directory[MAX_PATH_LENGTH];
    char output_directory[MAX_PATH_LENGTH];
    char report_directory[MAX_PATH_LENGTH];
    bool directories_created;
} FileIOContext;

/*============================================================================*/
/* FORWARD DECLARATIONS                                                       */
/*============================================================================*/

static FileIOContext g_file_io_context = {0};

static DecompStatus ReadFileToBuffer(const char* filepath, uint8_t** buffer, size_t* size);
static DecompStatus WriteBufferToFile(const char* filepath, const uint8_t* buffer, size_t size);
static DecompStatus GetFileInfoInternal(const char* filepath, FileInfo* info);
static DecompStatus CreateDirectoryRecursive(const char* path);
static bool DirectoryExists(const char* path);
static const char* GetFileExtension(const char* filename);
static DecompFileType DetectFileTypeFromExtension(const char* filename);
static DecompFileType DetectFileTypeFromContent(const uint8_t* data, size_t size);
// Byte order conversion functions are in byte_order.c

/*============================================================================*/
/* MODULE INITIALIZATION                                                      */
/*============================================================================*/

int FileIO_Init(void) {
    Logger_Log(LOG_LEVEL_INFO, "File I/O module initialized");
    
    // Initialize context
    memset(&g_file_io_context, 0, sizeof(FileIOContext));
    g_file_io_context.directories_created = false;
    
    return 0;
}

/*============================================================================*/
/* FILE READING FUNCTIONS                                                     */
/*============================================================================*/

DecompStatus FileIO_ReadCompressedFile(const char* filepath, uint8_t** buffer, size_t* size) {
    if (!filepath || !buffer || !size) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid parameters to FileIO_ReadCompressedFile");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "Reading compressed file: %s", filepath);
    
    // Get file size first
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to open file: %s", filepath);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid file size: %ld", file_size);
        fclose(file);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    if (file_size > DECOMP_MAX_REASONABLE) {
        Logger_Log(LOG_LEVEL_ERROR, "File too large: %ld bytes", file_size);
        fclose(file);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    // Allocate buffer
    *buffer = (uint8_t*)malloc(file_size);
    if (!*buffer) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to allocate buffer for file: %s", filepath);
        fclose(file);
        return DECOMP_STATUS_MEMORY_ERROR;
    }
    
    // Read file content
    size_t bytes_read = fread(*buffer, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to read complete file: %s (read %zu of %ld bytes)",
                  filepath, bytes_read, file_size);
        free(*buffer);
        *buffer = NULL;
        return DECOMP_STATUS_IO_ERROR;
    }
    
    *size = bytes_read;
    
    Logger_Log(LOG_LEVEL_INFO, "Successfully read file: %s (%zu bytes)", filepath, bytes_read);
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* FILE WRITING FUNCTIONS                                                     */
/*============================================================================*/

DecompStatus FileIO_WriteDecompressedFile(const char* filepath, const uint8_t* data, size_t size) {
    if (!filepath || !data || size == 0) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid parameters to FileIO_WriteDecompressedFile");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "Writing decompressed file: %s (%zu bytes)", filepath, size);
    
    // Create directory if needed
    char directory[MAX_PATH_LENGTH];
    strncpy(directory, filepath, MAX_PATH_LENGTH - 1);
    directory[MAX_PATH_LENGTH - 1] = '\0';
    
    // Find last directory separator
    char* last_slash = strrchr(directory, '/');
    if (!last_slash) {
        last_slash = strrchr(directory, '\\');
    }
    
    if (last_slash) {
        *last_slash = '\0';
        
        if (!DirectoryExists(directory)) {
            DecompStatus status = CreateDirectoryRecursive(directory);
            if (status != DECOMP_STATUS_SUCCESS) {
                Logger_Log(LOG_LEVEL_ERROR, "Failed to create directory: %s", directory);
                return status;
            }
        }
    }
    
    // Write file
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to create file: %s", filepath);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    size_t bytes_written = fwrite(data, 1, size, file);
    fclose(file);
    
    if (bytes_written != size) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to write complete file: %s (wrote %zu of %zu bytes)",
                  filepath, bytes_written, size);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    Logger_Log(LOG_LEVEL_INFO, "Successfully wrote file: %s (%zu bytes)", filepath, bytes_written);
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* FILE INFORMATION FUNCTIONS                                                 */
/*============================================================================*/

DecompStatus FileIO_GetFileInfo(const char* filepath, FileInfo* info) {
    if (!filepath || !info) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid parameters to FileIO_GetFileInfo");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    return GetFileInfoInternal(filepath, info);
}

static DecompStatus GetFileInfoInternal(const char* filepath, FileInfo* info) {
    Logger_Log(LOG_LEVEL_DEBUG, "Getting file info: %s", filepath);
    
    // Initialize info structure
    memset(info, 0, sizeof(FileInfo));
    
    // Extract filename from path
    const char* filename = strrchr(filepath, '/');
    if (!filename) {
        filename = strrchr(filepath, '\\');
    }
    if (filename) {
        filename++; // Skip the separator
    } else {
        filename = filepath;
    }
    
    strncpy(info->filename, filename, DECOMP_MAX_FILENAME - 1);
    info->filename[DECOMP_MAX_FILENAME - 1] = '\0';
    
    strncpy(info->filepath, filepath, DECOMP_MAX_PATH - 1);
    info->filepath[DECOMP_MAX_PATH - 1] = '\0';
    
    // Get file stats
    struct stat file_stat;
    if (stat(filepath, &file_stat) != 0) {
        Logger_Log(LOG_LEVEL_ERROR, "Failed to stat file: %s", filepath);
        return DECOMP_STATUS_IO_ERROR;
    }
    
    info->compressed_size = file_stat.st_size;
    info->type = DetectFileTypeFromExtension(filename);
    
    // Try to detect original size from header if it's a .comp file
    if (strstr(filename, ".comp") != NULL) {
        FILE* file = fopen(filepath, "rb");
        if (file) {
            // Read magic and version
            uint32_t magic;
            uint8_t version;
            
            if (fread(&magic, sizeof(uint32_t), 1, file) == 1 &&
                fread(&version, sizeof(uint8_t), 1, file) == 1) {
                
                magic = le32toh(magic);
                
                if (magic == 0x504D4F43) { // "COMP"
                    // Skip to original size field
                    if (version == 3) {
                        fseek(file, 5, SEEK_SET); // Skip magic(4) + version(1)
                        uint64_t original_size;
                        if (fread(&original_size, sizeof(uint64_t), 1, file) == 1) {
                            info->original_size = le64toh(original_size);
                            if (info->original_size > 0 && info->compressed_size > 0) {
                                info->compression_ratio = (double)info->compressed_size / info->original_size;
                            }
                        }
                    }
                }
            }
            fclose(file);
        }
    }
    
    Logger_Log(LOG_LEVEL_INFO, "File info for %s: size=%lu, type=%d, ratio=%.2f",
              filepath, info->compressed_size, info->type, info->compression_ratio);
    
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* DIRECTORY OPERATIONS                                                       */
/*============================================================================*/

DecompStatus FileIO_CreateOutputStructure(const char* output_dir) {
    if (!output_dir) {
        Logger_Log(LOG_LEVEL_ERROR, "Invalid output directory");
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "Creating output directory structure: %s", output_dir);
    
    // Create main output directory
    DecompStatus status = CreateDirectoryRecursive(output_dir);
    if (status != DECOMP_STATUS_SUCCESS) {
        return status;
    }
    
    // Create subdirectories
    char subdir[MAX_PATH_LENGTH];
    
    // Create decompressed files directory
    snprintf(subdir, sizeof(subdir), "%s/decompressed", output_dir);
    CreateDirectoryRecursive(subdir);
    
    // Create reports directory
    snprintf(subdir, sizeof(subdir), "%s/reports", output_dir);
    CreateDirectoryRecursive(subdir);
    
    // Create logs directory
    snprintf(subdir, sizeof(subdir), "%s/logs", output_dir);
    CreateDirectoryRecursive(subdir);
    
    // Store directories in context
    strncpy(g_file_io_context.output_directory, output_dir, MAX_PATH_LENGTH - 1);
    snprintf(g_file_io_context.report_directory, MAX_PATH_LENGTH, "%s/reports", output_dir);
    g_file_io_context.directories_created = true;
    
    Logger_Log(LOG_LEVEL_INFO, "Output directory structure created successfully");
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* DIRECTORY HELPER FUNCTIONS                                                 */
/*============================================================================*/

static DecompStatus CreateDirectoryRecursive(const char* path) {
    if (!path || strlen(path) == 0) {
        return DECOMP_STATUS_INVALID_ARGUMENT;
    }
    
    // Check if directory already exists
    if (DirectoryExists(path)) {
        return DECOMP_STATUS_SUCCESS;
    }
    
    // Create parent directories first
    char parent_path[MAX_PATH_LENGTH];
    strncpy(parent_path, path, MAX_PATH_LENGTH - 1);
    parent_path[MAX_PATH_LENGTH - 1] = '\0';
    
    // Remove trailing separators
    size_t len = strlen(parent_path);
    while (len > 0 && (parent_path[len-1] == '/' || parent_path[len-1] == '\\')) {
        parent_path[len-1] = '\0';
        len--;
    }
    
    // Find last separator
    char* last_sep = strrchr(parent_path, '/');
    if (!last_sep) {
        last_sep = strrchr(parent_path, '\\');
    }
    
    if (last_sep && last_sep != parent_path) {
        *last_sep = '\0';
        CreateDirectoryRecursive(parent_path);
    }
    
    // Create directory
    #ifdef _WIN32
        if (_mkdir(path) != 0 && errno != EEXIST) {
            Logger_Log(LOG_LEVEL_ERROR, "Failed to create directory: %s (errno=%d)", path, errno);
            return DECOMP_STATUS_IO_ERROR;
        }
    #else
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            Logger_Log(LOG_LEVEL_ERROR, "Failed to create directory: %s (errno=%d)", path, errno);
            return DECOMP_STATUS_IO_ERROR;
        }
    #endif
    
    return DECOMP_STATUS_SUCCESS;
}

static bool DirectoryExists(const char* path) {
    #ifdef _WIN32
        struct _stat st;
        return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR);
    #else
        struct stat st;
        return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    #endif
}

/*============================================================================*/
/* FILE TYPE DETECTION                                                        */
/*============================================================================*/

static const char* GetFileExtension(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return "";
    }
    return dot + 1;
}

static DecompFileType DetectFileTypeFromExtension(const char* filename) {
    const char* ext = GetFileExtension(filename);
    
    if (strcasecmp(ext, "txt") == 0) return FILE_TYPE_TEXT;
    if (strcasecmp(ext, "csv") == 0) return FILE_TYPE_CSV;
    if (strcasecmp(ext, "json") == 0) return FILE_TYPE_JSON;
    if (strcasecmp(ext, "xml") == 0) return FILE_TYPE_XML;
    if (strcasecmp(ext, "docx") == 0) return FILE_TYPE_DOCX;
    if (strcasecmp(ext, "pdf") == 0) return FILE_TYPE_PDF;
    if (strcasecmp(ext, "wav") == 0) return FILE_TYPE_AUDIO;
    if (strcasecmp(ext, "mp3") == 0) return FILE_TYPE_AUDIO;
    if (strcasecmp(ext, "bmp") == 0) return FILE_TYPE_BMP;
    if (strcasecmp(ext, "png") == 0) return FILE_TYPE_PNG;
    if (strcasecmp(ext, "jpg") == 0) return FILE_TYPE_IMAGE;
    if (strcasecmp(ext, "jpeg") == 0) return FILE_TYPE_IMAGE;
    if (strcasecmp(ext, "tga") == 0) return FILE_TYPE_TGA;
    if (strcasecmp(ext, "comp") == 0) return FILE_TYPE_BINARY;
    
    return FILE_TYPE_BINARY;
}

static DecompFileType DetectFileTypeFromContent(const uint8_t* data, size_t size) {
    if (!data || size < 4) {
        return FILE_TYPE_BINARY;
    }
    
    // Check for text files (printable ASCII characters)
    bool is_text = true;
    size_t sample_size = (size < 1024) ? size : 1024;
    
    for (size_t i = 0; i < sample_size; i++) {
        uint8_t ch = data[i];
        if (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t' && ch != 0) {
            is_text = false;
            break;
        }
    }
    
    if (is_text) {
        // Enhanced text type detection
        if (size >= 2) {
            // Check for JSON
            if ((data[0] == '{' && data[size-1] == '}') || 
                (data[0] == '[' && data[size-1] == ']')) {
                return FILE_TYPE_JSON;
            }
            
            // Check for XML
            if (size >= 5 && memcmp(data, "<?xml", 5) == 0) {
                return FILE_TYPE_XML;
            }
            
            // Check for CSV (simple heuristic)
            int comma_count = 0;
            int newline_count = 0;
            for (size_t i = 0; i < sample_size && i < 100; i++) {
                if (data[i] == ',') comma_count++;
                if (data[i] == '\n') newline_count++;
            }
            if (comma_count > 2 && newline_count > 0) {
                return FILE_TYPE_CSV;
            }
        }
        
        return FILE_TYPE_TEXT;
    }
    
    // Binary file detection
    if (size >= 4) {
        // PDF
        if ((size >= 7 && memcmp(data, "%PDF-", 5) == 0) || memcmp(data, "%PDF", 4) == 0) {
            return FILE_TYPE_PDF;
        }
        
        // PNG
        if (size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
            return FILE_TYPE_IMAGE;
        }
        
        // JPEG
        if (size >= 2 && memcmp(data, "\xFF\xD8", 2) == 0) {
            return FILE_TYPE_IMAGE;
        }
        
        // BMP
        if (size >= 2 && memcmp(data, "BM", 2) == 0) {
            return FILE_TYPE_IMAGE;
        }
        
        // GIF
        if (size >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) {
            return FILE_TYPE_IMAGE;
        }
        
        // WAV
        if (size >= 4 && memcmp(data, "RIFF", 4) == 0) {
            return FILE_TYPE_AUDIO;
        }
        
        // MP3
        if (size >= 3 && (memcmp(data, "ID3", 3) == 0 || memcmp(data, "\xFF\xFB", 2) == 0)) {
            return FILE_TYPE_AUDIO;
        }
        
        // DOCX (ZIP format)
        if (size >= 4 && memcmp(data, "PK\x03\x04", 4) == 0) {
            return FILE_TYPE_DOCX;
        }
    }
    
    return FILE_TYPE_BINARY;
}

/*============================================================================*/
/* BYTE ORDER CONVERSION HELPERS                                              */
/*============================================================================*/

// Use external byte order conversion functions

/*============================================================================*/
/* MODULE CLEANUP                                                             */
/*============================================================================*/

void FileIO_Cleanup(void) {
    Logger_Log(LOG_LEVEL_INFO, "File I/O module cleaned up");
    memset(&g_file_io_context, 0, sizeof(FileIOContext));
}