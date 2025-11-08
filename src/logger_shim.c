#include "../include/decompressor.h"

/* Minimal shim to satisfy logger dependencies without pulling full utility module */
const char* Utility_GetStatusDescription(DecompStatus status) {
    switch (status) {
        case DECOMP_STATUS_SUCCESS: return "SUCCESS";
        case DECOMP_STATUS_INVALID_MAGIC: return "INVALID_MAGIC";
        case DECOMP_STATUS_UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
        case DECOMP_STATUS_CORRUPTED_HEADER: return "CORRUPTED_HEADER";
        case DECOMP_STATUS_TRUNCATED_DATA: return "TRUNCATED_DATA";
        case DECOMP_STATUS_INVALID_ALGORITHM: return "INVALID_ALGORITHM";
        case DECOMP_STATUS_DECOMPRESSION_ERROR: return "DECOMPRESSION_ERROR";
        case DECOMP_STATUS_INTEGRITY_FAILURE: return "INTEGRITY_FAILURE";
        case DECOMP_STATUS_MEMORY_ERROR: return "MEMORY_ERROR";
        case DECOMP_STATUS_IO_ERROR: return "IO_ERROR";
        case DECOMP_STATUS_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        default: return "UNKNOWN";
    }
}