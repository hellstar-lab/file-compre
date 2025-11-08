/******************************************************************************
 * Simple Test Program for Decompression System
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

int main(int argc, char* argv[]) {
    printf("Simple Decompression Test\n");
    printf("========================\n\n");
    
    const char* directory = (argc > 1) ? argv[1] : "../output";
    printf("Scanning directory: %s\n", directory);
    
    DIR* dir = opendir(directory);
    if (!dir) {
        printf("ERROR: Cannot open directory: %s\n", directory);
        return 1;
    }
    
    struct dirent* entry;
    int file_count = 0;
    int comp_count = 0;
    
    printf("Files found:\n");
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; // Skip hidden files
        
        file_count++;
        
        // Check if it's a .comp file
        size_t len = strlen(entry->d_name);
        if (len > 5 && strcmp(entry->d_name + len - 5, ".comp") == 0) {
            comp_count++;
            printf("  [COMP] %s\n", entry->d_name);
        } else {
            printf("  %s\n", entry->d_name);
        }
    }
    
    closedir(dir);
    
    printf("\nSummary:\n");
    printf("  Total files: %d\n", file_count);
    printf("  .comp files: %d\n", comp_count);
    
    // Test file reading
    if (comp_count > 0) {
        printf("\nTesting file reading:\n");
        
        dir = opendir(directory);
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            
            size_t len = strlen(entry->d_name);
            if (len > 5 && strcmp(entry->d_name + len - 5, ".comp") == 0) {
                char filepath[1024];
                snprintf(filepath, sizeof(filepath), "%s/%s", directory, entry->d_name);
                
                printf("  Reading: %s\n", entry->d_name);
                
                FILE* file = fopen(filepath, "rb");
                if (file) {
                    // Read first 16 bytes
                    unsigned char buffer[16];
                    size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
                    fclose(file);
                    
                    printf("    Read %zu bytes: ", bytes_read);
                    for (size_t i = 0; i < bytes_read && i < 8; i++) {
                        printf("%02X ", buffer[i]);
                    }
                    printf("\n");
                    
                    // Check for magic numbers
                    if (bytes_read >= 4) {
                        if (memcmp(buffer, "COMP", 4) == 0) {
                            printf("    -> COMP format detected\n");
                        } else if (buffer[0] == 0xAD && buffer[1] == 0xEF && buffer[2] == 0x01) {
                            printf("    -> Hardcore format detected\n");
                        } else {
                            printf("    -> Unknown format\n");
                        }
                    }
                } else {
                    printf("    ERROR: Cannot open file\n");
                }
                
                break; // Just test one file
            }
        }
        closedir(dir);
    }
    
    printf("\nTest completed successfully!\n");
    return 0;
}