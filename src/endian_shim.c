#include <stdint.h>

// Provide little-endian to host conversion functions when not available
// Windows MinGW environments often lack <endian.h> with leXXtoh helpers.

uint16_t le16toh(uint16_t x) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return x;
#else
    return __builtin_bswap16(x);
#endif
}

uint32_t le32toh(uint32_t x) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return x;
#else
    return __builtin_bswap32(x);
#endif
}

uint64_t le64toh(uint64_t x) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return x;
#else
    return __builtin_bswap64(x);
#endif
}