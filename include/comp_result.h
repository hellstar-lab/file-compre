#ifndef COMP_RESULT_H
#define COMP_RESULT_H

/**
 * @file comp_result.h
 * @brief Comprehensive error handling system for Advanced File Compressor
 * 
 * This header defines a unified error handling system with 23 distinct error codes
 * covering all possible failure scenarios in the compression/decompression pipeline.
 * 
 * @author Advanced File Compressor Team
 * @date January 2025
 */

/**
 * @enum CompResult
 * @brief Comprehensive enumeration of all possible operation results
 * 
 * This enum provides a complete set of error codes for the entire compression system,
 * enabling precise error identification and handling throughout the codebase.
 */
typedef enum {
    // Success code
    COMP_OK = 0,                        ///< Operation completed successfully
    
    // General system errors (1-5)
    COMP_ERROR_MEMORY = 1,              ///< Memory allocation failure
    COMP_ERROR_INVALID_PARAM = 2,       ///< Invalid parameter passed to function
    COMP_ERROR_BUFFER_OVERFLOW = 3,     ///< Buffer overflow detected
    COMP_ERROR_INTERNAL = 4,            ///< Internal system error
    COMP_ERROR_NOT_IMPLEMENTED = 5,     ///< Feature not yet implemented
    
    // File I/O errors (6-10)
    COMP_ERROR_FILE_NOT_FOUND = 6,      ///< Input file does not exist
    COMP_ERROR_FILE_ACCESS = 7,         ///< File access permission denied
    COMP_ERROR_FILE_READ = 8,           ///< Error reading from file
    COMP_ERROR_FILE_WRITE = 9,          ///< Error writing to file
    COMP_ERROR_FILE_CORRUPT = 10,       ///< File is corrupted or invalid
    
    // Compression-specific errors (11-17)
    COMP_ERROR_ALGORITHM_UNKNOWN = 11,  ///< Unknown compression algorithm
    COMP_ERROR_COMPRESSION_FAILED = 12, ///< Compression operation failed
    COMP_ERROR_DECOMPRESSION_FAILED = 13, ///< Decompression operation failed
    COMP_ERROR_INVALID_FORMAT = 14,     ///< Invalid file format or header
    COMP_ERROR_SIZE_MISMATCH = 15,      ///< Size mismatch during operation
    COMP_ERROR_CHECKSUM_FAILED = 16,    ///< Checksum verification failed
    COMP_ERROR_UNSUPPORTED_FORMAT = 17, ///< Unsupported file format
    
    // Advanced algorithm errors (18-21)
    COMP_ERROR_HUFFMAN_TREE = 18,       ///< Huffman tree construction failed
    COMP_ERROR_LZ77_WINDOW = 19,        ///< LZ77 sliding window error
    COMP_ERROR_LZW_DICTIONARY = 20,     ///< LZW dictionary overflow
    COMP_ERROR_BWT_TRANSFORM = 21,      ///< Burrows-Wheeler Transform failed
    
    // Container and metadata errors (22-23)
    COMP_ERROR_CONTAINER_INVALID = 22,  ///< Invalid container structure
    COMP_ERROR_METADATA_CORRUPT = 23,    ///< Corrupted metadata

    // Synonyms for legacy naming used across the codebase
    // Success alias
    COMP_SUCCESS = COMP_OK,

    // General/system error aliases
    COMP_ERR_MEMORY = COMP_ERROR_MEMORY,
    COMP_ERR_INVALID_PARAMS = COMP_ERROR_INVALID_PARAM,
    COMP_ERR_BUFFER_OVERFLOW = COMP_ERROR_BUFFER_OVERFLOW,
    COMP_ERR_INTERNAL = COMP_ERROR_INTERNAL,
    COMP_ERR_NOT_IMPLEMENTED = COMP_ERROR_NOT_IMPLEMENTED,

    // File I/O error aliases
    COMP_ERR_FILE_NOT_FOUND = COMP_ERROR_FILE_NOT_FOUND,
    COMP_ERR_FILE_ACCESS = COMP_ERROR_FILE_ACCESS,
    COMP_ERR_FILE_READ = COMP_ERROR_FILE_READ,
    COMP_ERR_FILE_WRITE = COMP_ERROR_FILE_WRITE,
    COMP_ERR_FILE_CORRUPT = COMP_ERROR_FILE_CORRUPT,

    // Compression-specific error aliases
    COMP_ERR_INVALID_ALGORITHM = COMP_ERROR_ALGORITHM_UNKNOWN,
    COMP_ERR_COMPRESSION_FAILED = COMP_ERROR_COMPRESSION_FAILED,
    COMP_ERR_DECOMPRESSION_FAILED = COMP_ERROR_DECOMPRESSION_FAILED,
    COMP_ERR_INVALID_FORMAT = COMP_ERROR_INVALID_FORMAT,
    COMP_ERR_INVALID_SIZE = COMP_ERROR_SIZE_MISMATCH,
    COMP_ERR_CHECKSUM_FAILED = COMP_ERROR_CHECKSUM_FAILED,
    COMP_ERR_UNSUPPORTED_FORMAT = COMP_ERROR_UNSUPPORTED_FORMAT,

    // Additional alias used by hardcore LZMA refactor
    COMP_ERROR_COMPRESSION = COMP_ERROR_COMPRESSION_FAILED,

    // Advanced algorithm error aliases
    COMP_ERR_HUFFMAN_TREE = COMP_ERROR_HUFFMAN_TREE,
    COMP_ERR_LZ77_WINDOW = COMP_ERROR_LZ77_WINDOW,
    COMP_ERR_LZW_DICTIONARY = COMP_ERROR_LZW_DICTIONARY,
    COMP_ERR_BWT_TRANSFORM = COMP_ERROR_BWT_TRANSFORM,

    // Container/metadata aliases
    COMP_ERR_CONTAINER_INVALID = COMP_ERROR_CONTAINER_INVALID,
    COMP_ERR_METADATA_CORRUPT = COMP_ERROR_METADATA_CORRUPT
} CompResult;

/**
 * @brief Error checking macro that jumps to fail label on error
 * 
 * This macro wraps function calls and automatically handles error propagation
 * by jumping to a local 'fail' label when an error occurs.
 * 
 * @param call The function call to check
 */
#define COMP_CHECK(call) do { \
    CompResult _result = (call); \
    if (_result != COMP_OK) { \
        ret = _result; \
        goto fail; \
    } \
} while(0)

/**
 * @brief Simplified error checking macro for boolean returns
 * 
 * This macro is used for functions that return 0 on success and non-zero on failure.
 * 
 * @param call The function call to check
 * @param error_code The CompResult error code to assign on failure
 */
#define COMP_CHECK_BOOL(call, error_code) do { \
    if ((call) != 0) { \
        ret = (error_code); \
        goto fail; \
    } \
} while(0)

/**
 * @brief Error checking macro for pointer returns
 * 
 * This macro checks if a pointer is NULL and sets an appropriate error code.
 * 
 * @param ptr The pointer to check
 * @param error_code The CompResult error code to assign if NULL
 */
#define COMP_CHECK_PTR(ptr, error_code) do { \
    if ((ptr) == NULL) { \
        ret = (error_code); \
        goto fail; \
    } \
} while(0)

/**
 * @brief Memory allocation checking macro
 * 
 * This macro specifically checks memory allocation results.
 * 
 * @param ptr The allocated pointer to check
 */
#define COMP_CHECK_ALLOC(ptr) COMP_CHECK_PTR(ptr, COMP_ERROR_MEMORY)

/**
 * @brief Convert CompResult to human-readable string
 * 
 * @param result The CompResult error code
 * @return Constant string describing the error
 */
const char* comp_result_to_string(CompResult result);

/**
 * @brief Error tracing function for debugging
 * 
 * This function is called automatically by the COMP_CHECK macro to provide
 * detailed error tracing information for debugging purposes.
 * 
 * @param function_name Name of the function where error occurred
 * @param error_code The CompResult error code
 */
void comp_error_trace(const char* function_name, CompResult error_code);

/**
 * @brief Check if a CompResult indicates success
 * 
 * @param result The CompResult to check
 * @return 1 if successful, 0 if error
 */
#define COMP_SUCCESS(result) ((result) == COMP_OK)

/**
 * @brief Check if a CompResult indicates failure
 * 
 * @param result The CompResult to check
 * @return 1 if error, 0 if successful
 */
#define COMP_FAILED(result) ((result) != COMP_OK)

/**
 * @brief Standard fail label implementation
 * 
 * This macro provides a standard implementation for fail labels that should
 * be used consistently across the codebase.
 */
#define COMP_FAIL_LABEL(cleanup_code) \
fail: \
    comp_error_trace(__func__, ret); \
    cleanup_code; \
    return ret;

#endif // COMP_RESULT_H