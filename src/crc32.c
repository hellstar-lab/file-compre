/******************************************************************************
 * Advanced File Compressor - CRC32 Module
 * Description: Fast CRC32 checksum calculation for integrity verification
 ******************************************************************************/

#include "../include/decompressor.h"

/*============================================================================*/
/* CONSTANTS                                                                  */
/*============================================================================*/

#define CRC32_POLYNOMIAL    0xEDB88320UL
#define CRC32_INITIAL       0xFFFFFFFFUL
#define CRC32_FINAL_XOR     0xFFFFFFFFUL

/*============================================================================*/
/* GLOBAL VARIABLES                                                           */
/*============================================================================*/

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

/*============================================================================*/
/* PRIVATE FUNCTIONS                                                          */
/*============================================================================*/

static void CRC32_InitializeTable(void) {
    if (crc32_table_initialized) {
        return;
    }
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    
    crc32_table_initialized = true;
    Logger_Log(LOG_LEVEL_DEBUG, "CRC32 table initialized");
}

/*============================================================================*/
/* PUBLIC FUNCTIONS                                                           */
/*============================================================================*/

uint32_t CRC32_Calculate(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }
    
    CRC32_InitializeTable();
    
    uint32_t crc = CRC32_INITIAL;
    
    for (size_t i = 0; i < size; i++) {
        uint8_t table_index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[table_index];
    }
    
    return crc ^ CRC32_FINAL_XOR;
}

uint32_t CRC32_CalculateFile(const char* filepath) {
    if (!filepath) {
        Logger_Log(LOG_LEVEL_ERROR, "CRC32_CalculateFile: Invalid filepath");
        return 0;
    }
    
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        Logger_Log(LOG_LEVEL_ERROR, "CRC32_CalculateFile: Failed to open file: %s", filepath);
        return 0;
    }
    
    CRC32_InitializeTable();
    
    uint32_t crc = CRC32_INITIAL;
    uint8_t buffer[8192];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            uint8_t table_index = (crc ^ buffer[i]) & 0xFF;
            crc = (crc >> 8) ^ crc32_table[table_index];
        }
    }
    
    fclose(file);
    
    uint32_t result = crc ^ CRC32_FINAL_XOR;
    Logger_Log(LOG_LEVEL_DEBUG, "CRC32 calculated for file %s: 0x%08X", filepath, result);
    
    return result;
}

bool CRC32_Verify(const uint8_t* data, size_t size, uint32_t expected_crc) {
    if (!data) {
        Logger_Log(LOG_LEVEL_ERROR, "CRC32_Verify: Invalid data pointer");
        return false;
    }
    
    uint32_t calculated_crc = CRC32_Calculate(data, size);
    bool match = (calculated_crc == expected_crc);
    
    if (match) {
        Logger_Log(LOG_LEVEL_DEBUG, "CRC32 verification passed: 0x%08X", calculated_crc);
    } else {
        Logger_Log(LOG_LEVEL_ERROR, "CRC32 verification failed: expected 0x%08X, got 0x%08X", 
                   expected_crc, calculated_crc);
    }
    
    return match;
}

bool CRC32_VerifyFile(const char* filepath, uint32_t expected_crc) {
    if (!filepath) {
        Logger_Log(LOG_LEVEL_ERROR, "CRC32_VerifyFile: Invalid filepath");
        return false;
    }
    
    uint32_t calculated_crc = CRC32_CalculateFile(filepath);
    bool match = (calculated_crc == expected_crc);
    
    if (match) {
        Logger_Log(LOG_LEVEL_INFO, "File CRC32 verification passed: %s (0x%08X)", filepath, calculated_crc);
    } else {
        Logger_Log(LOG_LEVEL_ERROR, "File CRC32 verification failed: %s - expected 0x%08X, got 0x%08X", 
                   filepath, expected_crc, calculated_crc);
    }
    
    return match;
}

/*============================================================================*/
/* STREAMING CRC32 CALCULATION                                                */
/*============================================================================*/

CRC32Context* CRC32_CreateContext(void) {
    CRC32Context* ctx = (CRC32Context*)malloc(sizeof(CRC32Context));
    if (!ctx) {
        Logger_Log(LOG_LEVEL_ERROR, "CRC32_CreateContext: Memory allocation failed");
        return NULL;
    }
    
    CRC32_InitializeTable();
    
    ctx->crc = CRC32_INITIAL;
    ctx->initialized = true;
    
    Logger_Log(LOG_LEVEL_DEBUG, "CRC32 context created");
    return ctx;
}

void CRC32_UpdateContext(CRC32Context* ctx, const uint8_t* data, size_t size) {
    if (!ctx || !ctx->initialized || !data || size == 0) {
        return;
    }
    
    for (size_t i = 0; i < size; i++) {
        uint8_t table_index = (ctx->crc ^ data[i]) & 0xFF;
        ctx->crc = (ctx->crc >> 8) ^ crc32_table[table_index];
    }
}

uint32_t CRC32_FinalizeContext(CRC32Context* ctx) {
    if (!ctx || !ctx->initialized) {
        Logger_Log(LOG_LEVEL_ERROR, "CRC32_FinalizeContext: Invalid context");
        return 0;
    }
    
    uint32_t result = ctx->crc ^ CRC32_FINAL_XOR;
    Logger_Log(LOG_LEVEL_DEBUG, "CRC32 context finalized: 0x%08X", result);
    
    return result;
}

void CRC32_DestroyContext(CRC32Context* ctx) {
    if (!ctx) {
        return;
    }
    
    Logger_Log(LOG_LEVEL_DEBUG, "CRC32 context destroyed");
    free(ctx);
}

/*============================================================================*/
/* UTILITY FUNCTIONS                                                          */
/*============================================================================*/

void CRC32_PrintTable(void) {
    CRC32_InitializeTable();
    
    printf("CRC32 Table:\n");
    for (int i = 0; i < 256; i++) {
        if (i % 8 == 0) {
            printf("\n");
        }
        printf("0x%08X, ", crc32_table[i]);
    }
    printf("\n");
}

bool CRC32_SelfTest(void) {
    // Test with known data
    const char* test_data = "123456789";
    uint32_t expected = 0xCBF43926;
    uint32_t calculated = CRC32_Calculate((const uint8_t*)test_data, strlen(test_data));
    
    bool passed = (calculated == expected);
    
    if (passed) {
        Logger_Log(LOG_LEVEL_INFO, "CRC32 self-test passed");
    } else {
        Logger_Log(LOG_LEVEL_ERROR, "CRC32 self-test failed: expected 0x%08X, got 0x%08X", 
                   expected, calculated);
    }
    
    return passed;
}