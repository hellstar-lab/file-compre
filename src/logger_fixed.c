/******************************************************************************
 * Advanced File Compressor - Logger Module (Fixed)
 * Description: Comprehensive logging system with multiple output levels
 ******************************************************************************/

#include "../include/decompressor.h"
#include <stdarg.h>
#include <time.h>
#include <errno.h>

/*============================================================================*/
/* PRIVATE CONSTANTS                                                          */
/*============================================================================*/

#define LOG_BUFFER_SIZE         4096
#define LOG_FILENAME_LENGTH     256
#define LOG_TIMESTAMP_LENGTH    64
#define MAX_PATH_LENGTH         1024

/*============================================================================*/
/* PRIVATE STRUCTURES                                                         */
/*============================================================================*/

typedef struct {
    LogLevel current_level;
    FILE* file_handle;
    bool console_enabled;
    bool file_enabled;
    char log_filename[LOG_FILENAME_LENGTH];
    char log_directory[MAX_PATH_LENGTH];
    uint64_t messages_logged;
    uint64_t errors_logged;
    uint64_t warnings_logged;
} LoggerContext;

/*============================================================================*/
/* FORWARD DECLARATIONS                                                       */
/*============================================================================*/

static LoggerContext g_logger = {
    .current_level = LOG_LEVEL_INFO,
    .file_handle = NULL,
    .console_enabled = true,
    .file_enabled = false,
    .messages_logged = 0,
    .errors_logged = 0,
    .warnings_logged = 0
};

static const char* GetLevelString(LogLevel level);
static void GetCurrentTimestamp(char* buffer, size_t size);
static void FormatLogMessage(char* buffer, size_t size, LogLevel level, const char* format, va_list args);
static void WriteToConsole(const char* message, LogLevel level);
static void WriteToFile(const char* message);
static DecompStatus CreateLogDirectory(const char* directory);

/*============================================================================*/
/* MODULE INITIALIZATION                                                      */
/*============================================================================*/

int Logger_Init(const Logger* logger_config) {
    if (logger_config) {
        g_logger.current_level = logger_config->level;
        g_logger.console_enabled = logger_config->enable_console_output;
        g_logger.file_enabled = logger_config->enable_file_output;
        
        if (logger_config->log_file && strlen(logger_config->log_file) > 0) {
            strncpy(g_logger.log_filename, logger_config->log_file, LOG_FILENAME_LENGTH - 1);
            g_logger.log_filename[LOG_FILENAME_LENGTH - 1] = '\0';
        }
    }
    
    // Create log directory if file logging is enabled
    if (g_logger.file_enabled && strlen(g_logger.log_filename) > 0) {
        // Extract directory from filename
        char* last_slash = strrchr(g_logger.log_filename, '/');
        if (!last_slash) {
            last_slash = strrchr(g_logger.log_filename, '\\');
        }
        
        if (last_slash && last_slash != g_logger.log_filename) {
            size_t dir_len = last_slash - g_logger.log_filename;
            strncpy(g_logger.log_directory, g_logger.log_filename, dir_len);
            g_logger.log_directory[dir_len] = '\0';
            
            CreateLogDirectory(g_logger.log_directory);
        }
        
        // Open log file
        g_logger.file_handle = fopen(g_logger.log_filename, "a");
        if (!g_logger.file_handle) {
            g_logger.file_enabled = false;
            printf("[WARNING] Failed to open log file: %s\n", g_logger.log_filename);
        }
    }
    
    Logger_Log(LOG_LEVEL_INFO, "Logger module initialized (level=%s, console=%s, file=%s)",
              GetLevelString(g_logger.current_level),
              g_logger.console_enabled ? "enabled" : "disabled",
              g_logger.file_enabled ? "enabled" : "disabled");
    
    return 0;
}

/*============================================================================*/
/* MAIN LOGGING FUNCTIONS                                                     */
/*============================================================================*/

void Logger_Log(LogLevel level, const char* format, ...) {
    if (level > g_logger.current_level) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    char message[LOG_BUFFER_SIZE];
    FormatLogMessage(message, sizeof(message), level, format, args);
    
    va_end(args);
    
    // Write to console
    if (g_logger.console_enabled) {
        WriteToConsole(message, level);
    }
    
    // Write to file
    if (g_logger.file_enabled && g_logger.file_handle) {
        WriteToFile(message);
    }
    
    // Update statistics
    g_logger.messages_logged++;
    if (level == LOG_LEVEL_ERROR) {
        g_logger.errors_logged++;
    } else if (level == LOG_LEVEL_WARNING) {
        g_logger.warnings_logged++;
    }
}

void Logger_LogError(const char* function, DecompStatus status, const char* context) {
    const char* status_desc = Utility_GetStatusDescription(status);
    Logger_Log(LOG_LEVEL_ERROR, "[%s] %s (status=%s)", function, context, status_desc);
}

void Logger_LogFileStart(const char* filename) {
    Logger_Log(LOG_LEVEL_INFO, "========================================");
    Logger_Log(LOG_LEVEL_INFO, "Processing file: %s", filename);
    Logger_Log(LOG_LEVEL_INFO, "========================================");
}

void Logger_LogFileComplete(const char* filename, DecompStatus status, 
                           uint64_t original_size, uint64_t decompressed_size) {
    const char* status_desc = Utility_GetStatusDescription(status);
    
    if (status == DECOMP_STATUS_SUCCESS) {
        double ratio = 0.0;
        if (original_size > 0) {
            ratio = ((double)(original_size - decompressed_size) / original_size) * 100.0;
        }
        
        Logger_Log(LOG_LEVEL_INFO, "File processing completed successfully");
        Logger_Log(LOG_LEVEL_INFO, "Original size: %lu bytes", original_size);
        Logger_Log(LOG_LEVEL_INFO, "Decompressed size: %lu bytes", decompressed_size);
        Logger_Log(LOG_LEVEL_INFO, "Compression ratio: %.2f%%", ratio);
    } else {
        Logger_Log(LOG_LEVEL_ERROR, "File processing failed: %s", status_desc);
    }
    
    Logger_Log(LOG_LEVEL_INFO, "========================================");
}

/*============================================================================*/
/* PRIVATE HELPER FUNCTIONS                                                   */
/*============================================================================*/

static const char* GetLevelString(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_ERROR:   return "ERROR";
        case LOG_LEVEL_WARNING: return "WARNING";
        case LOG_LEVEL_INFO:    return "INFO";
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        case LOG_LEVEL_TRACE:   return "TRACE";
        default:                return "UNKNOWN";
    }
}

static void GetCurrentTimestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

static void FormatLogMessage(char* buffer, size_t size, LogLevel level, const char* format, va_list args) {
    char timestamp[LOG_TIMESTAMP_LENGTH];
    GetCurrentTimestamp(timestamp, sizeof(timestamp));
    
    char message[LOG_BUFFER_SIZE - 256]; // Reserve space for prefix
    vsnprintf(message, sizeof(message), format, args);
    
    snprintf(buffer, size, "[%s] [%s] %s", timestamp, GetLevelString(level), message);
}

static void WriteToConsole(const char* message, LogLevel level) {
    // Simple console output without Windows-specific features
    printf("%s\n", message);
    fflush(stdout);
}

static void WriteToFile(const char* message) {
    if (!g_logger.file_handle) {
        return;
    }
    
    fprintf(g_logger.file_handle, "%s\n", message);
    fflush(g_logger.file_handle); // Ensure immediate write
}

static DecompStatus CreateLogDirectory(const char* directory) {
    #ifdef _WIN32
        // Windows directory creation
        char temp_path[MAX_PATH_LENGTH];
        strncpy(temp_path, directory, MAX_PATH_LENGTH - 1);
        temp_path[MAX_PATH_LENGTH - 1] = '\0';
        
        // Create directories recursively
        for (char* p = temp_path; *p; p++) {
            if (*p == '/' || *p == '\\') {
                char separator = *p;
                *p = '\0';
                
                if (strlen(temp_path) > 0) {
                    _mkdir(temp_path);
                }
                
                *p = separator;
            }
        }
        
        // Create final directory
        if (_mkdir(temp_path) != 0 && errno != EEXIST) {
            return DECOMP_STATUS_IO_ERROR;
        }
    #else
        // Unix-like directory creation
        char temp_path[MAX_PATH_LENGTH];
        strncpy(temp_path, directory, MAX_PATH_LENGTH - 1);
        temp_path[MAX_PATH_LENGTH - 1] = '\0';
        
        // Create directories recursively
        for (char* p = temp_path; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                
                if (strlen(temp_path) > 0) {
                    mkdir(temp_path, 0755);
                }
                
                *p = '/';
            }
        }
        
        // Create final directory
        if (mkdir(temp_path, 0755) != 0 && errno != EEXIST) {
            return DECOMP_STATUS_IO_ERROR;
        }
    #endif
    
    return DECOMP_STATUS_SUCCESS;
}

/*============================================================================*/
/* LOGGING STATISTICS                                                         */
/*============================================================================*/

void Logger_GetStats(uint64_t* total_messages, uint64_t* errors, uint64_t* warnings) {
    if (total_messages) *total_messages = g_logger.messages_logged;
    if (errors) *errors = g_logger.errors_logged;
    if (warnings) *warnings = g_logger.warnings_logged;
}

/*============================================================================*/
/* MODULE CLEANUP                                                             */
/*============================================================================*/

void Logger_Cleanup(void) {
    Logger_Log(LOG_LEVEL_INFO, "Logger module cleanup - Total messages: %lu, Errors: %lu, Warnings: %lu",
              g_logger.messages_logged, g_logger.errors_logged, g_logger.warnings_logged);
    
    if (g_logger.file_handle) {
        fclose(g_logger.file_handle);
        g_logger.file_handle = NULL;
    }
    
    g_logger.file_enabled = false;
    g_logger.console_enabled = false;
}