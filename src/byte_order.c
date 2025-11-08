/******************************************************************************
 * Advanced File Compressor - Byte Order Utilities
 * Description: Cross-platform byte order conversion functions
 ******************************************************************************/

#include "../include/decompressor.h"

/*============================================================================*/
/* BYTE ORDER CONVERSION FUNCTIONS                                            */
/*============================================================================*/

uint32_t le32toh(uint32_t value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return value;
    #else
        return ((value & 0xFF000000) >> 24) |
               ((value & 0x00FF0000) >> 8)  |
               ((value & 0x0000FF00) << 8)  |
               ((value & 0x000000FF) << 24);
    #endif
}

uint64_t le64toh(uint64_t value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return value;
    #else
        return ((value & 0xFF00000000000000ULL) >> 56) |
               ((value & 0x00FF000000000000ULL) >> 40) |
               ((value & 0x0000FF0000000000ULL) >> 24) |
               ((value & 0x000000FF00000000ULL) >> 8)  |
               ((value & 0x00000000FF000000ULL) << 8)  |
               ((value & 0x0000000000FF0000ULL) << 24) |
               ((value & 0x000000000000FF00ULL) << 40) |
               ((value & 0x00000000000000FFULL) << 56);
    #endif
}

uint32_t htole32(uint32_t value) {
    return le32toh(value); // Same conversion
}

uint64_t htole64(uint64_t value) {
    return le64toh(value); // Same conversion
}

uint16_t le16toh(uint16_t value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return value;
    #else
        return ((value & 0xFF00) >> 8) |
               ((value & 0x00FF) << 8);
    #endif
}

uint16_t htole16(uint16_t value) {
    return le16toh(value); // Same conversion
}