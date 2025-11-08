/******************************************************************************
 * Advanced File Compressor - Bit I/O Module
 * Description: Robust bit-level reading with buffering and error handling
 ******************************************************************************/

#include "../include/decompressor.h"

/*============================================================================*/
/* CONSTANTS                                                                  */
/*============================================================================*/

#define BITIO_BUFFER_SIZE       8192
#define BITIO_BITS_PER_BYTE     8
#define BITIO_MAX_BITS          32

/*============================================================================*/
/* STRUCTURES                                                                 */
/*============================================================================*/

struct BitReader {
    FILE* file;
    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_pos;
    size_t buffer_end;
    uint32_t bit_buffer;
    int bits_available;
    bool eof_reached;
    bool error_state;
    uint64_t total_bits_read;
    uint64_t total_bytes_read;
};

/*============================================================================*/
/* PRIVATE FUNCTIONS                                                          */
/*============================================================================*/

static bool BitReader_FillBuffer(BitReader* br) {
    if (!br || br->error_state || br->eof_reached) {
        return false;
    }
    
    // Move remaining data to beginning of buffer
    if (br->buffer_pos < br->buffer_end) {
        size_t remaining = br->buffer_end - br->buffer_pos;
        memmove(br->buffer, br->buffer + br->buffer_pos, remaining);
        br->buffer_end = remaining;
    } else {
        br->buffer_end = 0;
    }
    br->buffer_pos = 0;
    
    // Read new data
    size_t bytes_to_read = br->buffer_size - br->buffer_end;
    size_t bytes_read = fread(br->buffer + br->buffer_end, 1, bytes_to_read, br->file);
    
    if (bytes_read == 0) {
        if (feof(br->file)) {
            br->eof_reached = true;
        } else {
            br->error_state = true;
            Logger_Log(LOG_LEVEL_ERROR, "BitReader: Error reading from file");
        }
        return false;
    }
    
    br->buffer_end += bytes_read;
    br->total_bytes_read += bytes_read;
    return true;
}

static bool BitReader_EnsureBits(BitReader* br, int bits_needed) {
    if (!br || br->error_state || bits_needed > BITIO_MAX_BITS) {
        return false;
    }
    
    while (br->bits_available < bits_needed) {
        // Need more bits
        if (br->buffer_pos >= br->buffer_end) {
            if (!BitReader_FillBuffer(br)) {
                return false;
            }
        }
        
        if (br->buffer_pos < br->buffer_end) {
            // Add byte to bit buffer
            br->bit_buffer = (br->bit_buffer << BITIO_BITS_PER_BYTE) | br->buffer[br->buffer_pos++];
            br->bits_available += BITIO_BITS_PER_BYTE;
        } else {
            break;
        }
    }
    
    return br->bits_available >= bits_needed;
}

/*============================================================================*/
/* PUBLIC FUNCTIONS                                                           */
/*============================================================================*/

BitReader* BitReader_Create(FILE* file) {
    if (!file) {
        Logger_Log(LOG_LEVEL_ERROR, "BitReader_Create: Invalid file pointer");
        return NULL;
    }
    
    BitReader* br = (BitReader*)malloc(sizeof(BitReader));
    if (!br) {
        Logger_Log(LOG_LEVEL_ERROR, "BitReader_Create: Memory allocation failed");
        return NULL;
    }
    
    br->buffer = (uint8_t*)malloc(BITIO_BUFFER_SIZE);
    if (!br->buffer) {
        Logger_Log(LOG_LEVEL_ERROR, "BitReader_Create: Buffer allocation failed");
        free(br);
        return NULL;
    }
    
    br->file = file;
    br->buffer_size = BITIO_BUFFER_SIZE;
    br->buffer_pos = 0;
    br->buffer_end = 0;
    br->bit_buffer = 0;
    br->bits_available = 0;
    br->eof_reached = false;
    br->error_state = false;
    br->total_bits_read = 0;
    br->total_bytes_read = 0;
    
    Logger_Log(LOG_LEVEL_DEBUG, "BitReader created successfully");
    return br;
}

BitReader* BitReader_CreateFromBuffer(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        Logger_Log(LOG_LEVEL_ERROR, "BitReader_CreateFromBuffer: Invalid parameters");
        return NULL;
    }
    
    BitReader* br = (BitReader*)malloc(sizeof(BitReader));
    if (!br) {
        Logger_Log(LOG_LEVEL_ERROR, "BitReader_CreateFromBuffer: Memory allocation failed");
        return NULL;
    }
    
    br->buffer = (uint8_t*)malloc(size);
    if (!br->buffer) {
        Logger_Log(LOG_LEVEL_ERROR, "BitReader_CreateFromBuffer: Buffer allocation failed");
        free(br);
        return NULL;
    }
    
    memcpy(br->buffer, data, size);
    
    br->file = NULL;
    br->buffer_size = size;
    br->buffer_pos = 0;
    br->buffer_end = size;
    br->bit_buffer = 0;
    br->bits_available = 0;
    br->eof_reached = false;
    br->error_state = false;
    br->total_bits_read = 0;
    br->total_bytes_read = 0;
    
    Logger_Log(LOG_LEVEL_DEBUG, "BitReader created from buffer (size: %zu)", size);
    return br;
}

int BitReader_ReadBit(BitReader* br) {
    if (!BitReader_EnsureBits(br, 1)) {
        return -1;
    }
    
    br->bits_available--;
    int bit = (br->bit_buffer >> br->bits_available) & 1;
    br->total_bits_read++;
    
    return bit;
}

uint32_t BitReader_ReadBits(BitReader* br, int num_bits) {
    if (!br || num_bits <= 0 || num_bits > BITIO_MAX_BITS) {
        Logger_Log(LOG_LEVEL_ERROR, "BitReader_ReadBits: Invalid parameters");
        return 0;
    }
    
    if (!BitReader_EnsureBits(br, num_bits)) {
        Logger_Log(LOG_LEVEL_WARNING, "BitReader_ReadBits: Not enough bits available");
        return 0;
    }
    
    br->bits_available -= num_bits;
    uint32_t mask = (1U << num_bits) - 1;
    uint32_t result = (br->bit_buffer >> br->bits_available) & mask;
    br->total_bits_read += num_bits;
    
    return result;
}

uint8_t BitReader_ReadByte(BitReader* br) {
    return (uint8_t)BitReader_ReadBits(br, 8);
}

bool BitReader_SkipBits(BitReader* br, int num_bits) {
    if (!br || num_bits < 0) {
        return false;
    }
    
    while (num_bits > 0) {
        int bits_to_skip = (num_bits > BITIO_MAX_BITS) ? BITIO_MAX_BITS : num_bits;
        if (!BitReader_EnsureBits(br, bits_to_skip)) {
            return false;
        }
        br->bits_available -= bits_to_skip;
        br->total_bits_read += bits_to_skip;
        num_bits -= bits_to_skip;
    }
    
    return true;
}

bool BitReader_AlignToByte(BitReader* br) {
    if (!br) {
        return false;
    }
    
    int bits_to_skip = br->bits_available % BITIO_BITS_PER_BYTE;
    if (bits_to_skip > 0) {
        return BitReader_SkipBits(br, bits_to_skip);
    }
    
    return true;
}

bool BitReader_IsEOF(BitReader* br) {
    if (!br) {
        return true;
    }
    
    return br->eof_reached && br->bits_available == 0;
}

bool BitReader_HasError(BitReader* br) {
    if (!br) {
        return true;
    }
    
    return br->error_state;
}

uint64_t BitReader_GetBitsRead(BitReader* br) {
    if (!br) {
        return 0;
    }
    
    return br->total_bits_read;
}

uint64_t BitReader_GetBytesRead(BitReader* br) {
    if (!br) {
        return 0;
    }
    
    return br->total_bytes_read;
}

void BitReader_Destroy(BitReader* br) {
    if (!br) {
        return;
    }
    
    if (br->buffer) {
        free(br->buffer);
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "BitReader destroyed (bits read: %llu, bytes read: %llu)", 
               br->total_bits_read, br->total_bytes_read);
    
    free(br);
}

/*============================================================================*/
/* DEBUGGING AND DIAGNOSTICS                                                  */
/*============================================================================*/

void BitReader_DebugSync(BitReader* br, const char* context) {
    if (!br) {
        return;
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "BitReader sync [%s]: pos=%zu/%zu, bits=%d, total_bits=%llu, total_bytes=%llu",
               context ? context : "unknown",
               br->buffer_pos, br->buffer_end,
               br->bits_available,
               br->total_bits_read,
               br->total_bytes_read);
}

bool BitReader_Seek(BitReader* br, uint64_t bit_position) {
    if (!br || !br->file) {
        return false;
    }
    
    uint64_t byte_position = bit_position / BITIO_BITS_PER_BYTE;
    int bit_offset = bit_position % BITIO_BITS_PER_BYTE;
    
    if (fseek(br->file, (long)byte_position, SEEK_SET) != 0) {
        br->error_state = true;
        return false;
    }
    
    // Reset state
    br->buffer_pos = 0;
    br->buffer_end = 0;
    br->bit_buffer = 0;
    br->bits_available = 0;
    br->eof_reached = false;
    br->error_state = false;
    br->total_bits_read = bit_position;
    br->total_bytes_read = byte_position;
    
    // Skip to bit offset
    if (bit_offset > 0) {
        return BitReader_SkipBits(br, bit_offset);
    }
    
    return true;
}