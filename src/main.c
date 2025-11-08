#include "../include/compressor.h"
#include <setjmp.h>

// Forward declaration for GUI
int StartGUI();

// External declarations for memory pool
extern jmp_buf g_panic_buf;
extern void comp_check_leaks(void);

int main(int argc, char* argv[]) {
    printf("\n=== Professional File Compressor v2.0 ===\n");
    printf("Advanced Lossless Compression with Multi-Level Settings\n");
    printf("Supported formats: TXT, PDF, XML, Audio, CSV, JSON, Images\n\n");
    
    // Set up panic recovery point
    if (setjmp(g_panic_buf) != 0) {
        printf("\n*** PANIC RECOVERY ***\n");
        printf("A critical memory allocation failure occurred.\n");
        printf("The program has recovered and will attempt to continue.\n");
        printf("Some operations may have been aborted.\n\n");
        // Continue execution after panic
    }
    
    // Register leak detector to run on exit
    atexit(comp_check_leaks);
    
    // Check for GUI mode
    if (argc > 1 && strcmp(argv[1], "--gui") == 0) {
        return StartGUI();
    }
    
    int choice;
    char input_path[MAX_PATH_LENGTH];
    char output_path[MAX_PATH_LENGTH];
    char dest_path[MAX_PATH_LENGTH];
    CompressionStats stats;
    
    while (1) {
        print_menu();
        choice = get_user_choice();
        
        switch (choice) {
            case 1: { // Upload and Compress File
                printf("\nEnter the full path of the file to compress: ");
                fgets(input_path, sizeof(input_path), stdin);
                input_path[strcspn(input_path, "\n")] = 0; // Remove newline
                
                // Copy file to data folder
                if (copy_file_to_data_folder(input_path, dest_path) != 0) {
                    printf("Error: Could not copy file to data folder.\n");
                    break;
                }
                
                // Use HARDCORE compression by default for better space savings
                printf("\n*** HARDCORE COMPRESSION MODE ENABLED BY DEFAULT ***\n");
                printf("This uses a 5-stage pipeline: BWT -> MTF -> RLE -> Dictionary -> Huffman\n");
                printf("Expected space savings: 40-60%%\n");
                printf("This will achieve maximum compression with optimal space savings!\n\n");
                
                CompressionAlgorithm algo = ALGO_HARDCORE;
                CompressionLevel level = COMPRESSION_LEVEL_HIGH;
                
                // Ask user for compression mode
                printf("Select compression mode:\n");
                printf("1. INTELLIGENT compression (Auto-selects best algorithm, prevents size increases)\n");
                printf("2. HARDCORE compression (40-60%% space savings - slowest but best!)\n");
                printf("3. Manual algorithm selection\n");
                printf("Enter choice (1-3): ");
                char choice_buffer[10];
                fgets(choice_buffer, sizeof(choice_buffer), stdin);
                int mode_choice = atoi(choice_buffer);

                // Generate output filename
                generate_compressed_filename(dest_path, output_path);
                
                printf("Compressing file...\n");
                
                if (mode_choice == 1) {
                    // Use intelligent compression
                    printf("Using INTELLIGENT compression mode...\n");
                    printf("This will test multiple algorithms and select the best one.\n");
                    printf("Compression ratio validation ensures no file size increases.\n\n");
                    
                    if (compress_file_intelligent(dest_path, output_path, level, &stats) == 0) {
                        printf("\n✓ File compressed successfully with intelligent algorithm selection!\n");
                        print_compression_stats(&stats);
                    } else {
                        printf("✗ Intelligent compression failed!\n");
                    }
                } else if (mode_choice == 2) {
                    // Use hardcore compression
                    if (compress_file_with_level(dest_path, output_path, algo, level, &stats) == 0) {
                        printf("\n✓ File compressed successfully with HARDCORE compression!\n");
                        print_compression_stats(&stats);
                    } else {
                        printf("✗ HARDCORE compression failed!\n");
                    }
                } else {
                    // Manual algorithm selection
                    printf("\nSelect algorithm:\n");
                    printf("1. Huffman (good for text)\n");
                    printf("2. LZ77 (good for general files)\n");
                    printf("3. LZW (good for repetitive data)\n");
                    printf("4. Audio Advanced (for audio files)\n");
                    printf("5. Image Advanced (for image files)\n");
                    printf("6. HARDCORE (maximum compression)\n");
                    printf("Enter choice (1-6): ");
                    
                    int algo_choice = get_user_choice();
                    switch (algo_choice) {
                        case 1: algo = ALGO_HUFFMAN; break;
                        case 2: algo = ALGO_LZ77; break;
                        case 3: algo = ALGO_LZW; break;
                        case 4: algo = ALGO_AUDIO_ADVANCED; break;
                        case 5: algo = ALGO_IMAGE_ADVANCED; break;
                        case 6: algo = ALGO_HARDCORE; break;
                        default: algo = ALGO_HARDCORE; break;
                    }
                    
                    if (compress_file_with_level(dest_path, output_path, algo, level, &stats) == 0) {
                        printf("\n✓ File compressed successfully!\n");
                        print_compression_stats(&stats);
                    } else {
                        printf("✗ Compression failed!\n");
                    }
                }
                break;
            }
            
            case 2: { // Decompress File
                printf("\n=== Available Compressed Files ===\n");
                list_files_in_directory("output");
                
                printf("\nEnter the filename from output folder to decompress: ");
                char filename[MAX_FILENAME_LENGTH];
                fgets(filename, sizeof(filename), stdin);
                filename[strcspn(filename, "\n")] = 0;
                
                // Construct full path to compressed file in output folder
                snprintf(input_path, sizeof(input_path), "output\\%s", filename);
                
                // Check if file exists
                FILE* test_file = fopen(input_path, "rb");
                if (test_file == NULL) {
                    printf("✗ File not found in output folder!\n");
                    break;
                }
                fclose(test_file);
                
                printf("Decompressing file...\n");
                
                // First, we need to read the header to get the original extension
                FILE* comp_file = fopen(input_path, "rb");
                if (comp_file) {
                    unsigned char header[32];
                    if (fread(header, 1, 32, comp_file) == 32) {
                        // Extract original extension from header
                        char original_extension[16];
                        memset(original_extension, 0, sizeof(original_extension));
                        strncpy(original_extension, (char*)&header[13], 15);
                        original_extension[15] = '\0';
                        
                        // Generate output filename with original extension
                        generate_decompressed_filename_with_ext(input_path, original_extension, output_path);
                    } else {
                        // Fallback to old method if header read fails
                        generate_decompressed_filename(input_path, output_path);
                    }
                    fclose(comp_file);
                } else {
                    // Fallback to old method if file can't be opened
                    generate_decompressed_filename(input_path, output_path);
                }
                
                if (decompress_file(input_path, output_path, &stats) == 0) {
                    printf("\n✓ File decompressed successfully!\n");
                    printf("Decompressed file saved as: %s\n", output_path);
                } else {
                    printf("✗ Decompression failed!\n");
                }
                break;
            }
            
            case 3: { // List Files in Data Folder
                printf("\n=== Files in Data Folder ===\n");
                list_files_in_directory("data");
                break;
            }
            
            case 4: { // List Files in Output Folder
                printf("\n=== Files in Output Folder ===\n");
                list_files_in_directory("output");
                break;
            }
            
            case 5: { // Download File
                printf("\nAvailable files for download:\n");
                printf("1. From Data folder (original/decompressed files)\n");
                printf("2. From Output folder (compressed files)\n");
                printf("Choose folder (1-2): ");
                
                int folder_choice;
                scanf("%d", &folder_choice);
                getchar(); // consume newline
                
                if (folder_choice == 1) {
                    list_files_in_directory("data");
                } else if (folder_choice == 2) {
                    list_files_in_directory("output");
                } else {
                    printf("Invalid choice!\n");
                    break;
                }
                
                printf("\nEnter filename to download: ");
                char filename[MAX_FILENAME_LENGTH];
                fgets(filename, sizeof(filename), stdin);
                filename[strcspn(filename, "\n")] = 0;
                
                printf("Enter destination path: ");
                fgets(output_path, sizeof(output_path), stdin);
                output_path[strcspn(output_path, "\n")] = 0;
                
                // Construct source path
                char source_path[MAX_PATH_LENGTH];
                if (folder_choice == 1) {
                    snprintf(source_path, sizeof(source_path), "data\\%s", filename);
                } else {
                    snprintf(source_path, sizeof(source_path), "output\\%s", filename);
                }
                
                // Copy file to destination
                unsigned char* buffer;
                long size;
                if (read_file(source_path, &buffer, &size) == 0) {
                    if (write_file(output_path, buffer, size) == 0) {
                        printf("✓ File downloaded successfully to: %s\n", output_path);
                    } else {
                        printf("✗ Failed to write file to destination!\n");
                    }
                    free(buffer);
                } else {
                    printf("✗ File not found or could not be read!\n");
                }
                break;
            }
            
            case 6: { // Delete File
                printf("\nChoose folder to delete from:\n");
                printf("1. Data folder\n");
                printf("2. Output folder\n");
                printf("Choose (1-2): ");
                
                int folder_choice;
                scanf("%d", &folder_choice);
                getchar();
                
                if (folder_choice == 1) {
                    list_files_in_directory("data");
                } else if (folder_choice == 2) {
                    list_files_in_directory("output");
                } else {
                    printf("Invalid choice!\n");
                    break;
                }
                
                printf("\nEnter filename to delete: ");
                char filename[MAX_FILENAME_LENGTH];
                fgets(filename, sizeof(filename), stdin);
                filename[strcspn(filename, "\n")] = 0;
                
                char file_path[MAX_PATH_LENGTH];
                if (folder_choice == 1) {
                    snprintf(file_path, sizeof(file_path), "data\\%s", filename);
                } else {
                    snprintf(file_path, sizeof(file_path), "output\\%s", filename);
                }
                
                if (delete_file(file_path) == 0) {
                    printf("✓ File deleted successfully!\n");
                } else {
                    printf("✗ Failed to delete file!\n");
                }
                break;
            }
            
            case 0: // Exit
                printf("\nThank you for using File Compressor!\n");
                printf("PBL Project - DSA Implementation\n");
                return 0;
                
            default:
                printf("Invalid choice! Please try again.\n");
                break;
        }
        
        printf("\nPress Enter to continue...");
        getchar();
    }
    
    return 0;
}