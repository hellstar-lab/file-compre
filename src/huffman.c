#include "../include/compressor.h"

// Create a new Huffman node
HuffmanNode* create_huffman_node(unsigned char data, unsigned int frequency) {
    HuffmanNode* node = (HuffmanNode*)malloc(sizeof(HuffmanNode));
    if (!node) return NULL;
    
    node->data = data;
    node->frequency = frequency;
    node->left = NULL;
    node->right = NULL;
    return node;
}

// Min heap functions for Huffman tree construction
typedef struct {
    HuffmanNode** nodes;
    int size;
    int capacity;
} MinHeap;

MinHeap* create_min_heap(int capacity) {
    MinHeap* heap = (MinHeap*)malloc(sizeof(MinHeap));
    heap->nodes = (HuffmanNode**)malloc(capacity * sizeof(HuffmanNode*));
    heap->size = 0;
    heap->capacity = capacity;
    return heap;
}

void swap_nodes(HuffmanNode** a, HuffmanNode** b) {
    HuffmanNode* temp = *a;
    *a = *b;
    *b = temp;
}

void min_heapify(MinHeap* heap, int idx) {
    int smallest = idx;
    int left = 2 * idx + 1;
    int right = 2 * idx + 2;
    
    if (left < heap->size && heap->nodes[left]->frequency < heap->nodes[smallest]->frequency)
        smallest = left;
    
    if (right < heap->size && heap->nodes[right]->frequency < heap->nodes[smallest]->frequency)
        smallest = right;
    
    if (smallest != idx) {
        swap_nodes(&heap->nodes[smallest], &heap->nodes[idx]);
        min_heapify(heap, smallest);
    }
}

HuffmanNode* extract_min(MinHeap* heap) {
    if (heap->size <= 0) return NULL;
    
    HuffmanNode* root = heap->nodes[0];
    heap->nodes[0] = heap->nodes[heap->size - 1];
    heap->size--;
    min_heapify(heap, 0);
    
    return root;
}

void insert_min_heap(MinHeap* heap, HuffmanNode* node) {
    if (heap->size >= heap->capacity) return;
    
    int i = heap->size;
    heap->size++;
    
    while (i && node->frequency < heap->nodes[(i - 1) / 2]->frequency) {
        heap->nodes[i] = heap->nodes[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    
    heap->nodes[i] = node;
}

// Build Huffman tree from frequency array
HuffmanNode* build_huffman_tree(unsigned int* frequencies) {
    MinHeap* heap = create_min_heap(256);
    
    // Create leaf nodes for all characters with non-zero frequency
    for (int i = 0; i < 256; i++) {
        if (frequencies[i] > 0) {
            HuffmanNode* node = create_huffman_node((unsigned char)i, frequencies[i]);
            insert_min_heap(heap, node);
        }
    }
    
    // Build the tree
    while (heap->size > 1) {
        HuffmanNode* left = extract_min(heap);
        HuffmanNode* right = extract_min(heap);
        
        HuffmanNode* merged = create_huffman_node(0, left->frequency + right->frequency);
        merged->left = left;
        merged->right = right;
        
        insert_min_heap(heap, merged);
    }
    
    HuffmanNode* root = extract_min(heap);
    free(heap->nodes);
    free(heap);
    
    return root;
}

// Generate Huffman codes for each character
void generate_huffman_codes(HuffmanNode* root, char codes[256][256], char* current_code, int depth) {
    if (!root) return;
    
    // If this is a leaf node, store the code
    if (!root->left && !root->right) {
        if (depth == 0) {
            // Special case: only one character in the file
            strcpy(codes[root->data], "0");
        } else {
            current_code[depth] = '\0';
            strcpy(codes[root->data], current_code);
        }
        return;
    }
    
    // Traverse left (add '0')
    if (root->left) {
        current_code[depth] = '0';
        generate_huffman_codes(root->left, codes, current_code, depth + 1);
    }
    
    // Traverse right (add '1')
    if (root->right) {
        current_code[depth] = '1';
        generate_huffman_codes(root->right, codes, current_code, depth + 1);
    }
}

// Write bits to output buffer
typedef struct {
    unsigned char* buffer;
    long size;
    long capacity;
    int bit_count;
    unsigned char current_byte;
} BitWriter;

BitWriter* create_bit_writer(long initial_capacity) {
    BitWriter* writer = (BitWriter*)malloc(sizeof(BitWriter));
    writer->buffer = (unsigned char*)malloc(initial_capacity);
    writer->size = 0;
    writer->capacity = initial_capacity;
    writer->bit_count = 0;
    writer->current_byte = 0;
    return writer;
}

void write_bit(BitWriter* writer, int bit) {
    writer->current_byte = (writer->current_byte << 1) | (bit & 1);
    writer->bit_count++;
    
    if (writer->bit_count == 8) {
        if (writer->size >= writer->capacity) {
            writer->capacity *= 2;
            writer->buffer = (unsigned char*)realloc(writer->buffer, writer->capacity);
        }
        writer->buffer[writer->size++] = writer->current_byte;
        writer->bit_count = 0;
        writer->current_byte = 0;
    }
}

void flush_bits(BitWriter* writer) {
    if (writer->bit_count > 0) {
        writer->current_byte <<= (8 - writer->bit_count);
        if (writer->size >= writer->capacity) {
            writer->capacity *= 2;
            writer->buffer = (unsigned char*)realloc(writer->buffer, writer->capacity);
        }
        writer->buffer[writer->size++] = writer->current_byte;
    }
}

// Huffman compression function
CompResult huffman_compress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    if (!input || input_size <= 0) return COMP_ERR_INVALID_PARAMS;
    
    // Calculate character frequencies
    unsigned int frequencies[256] = {0};
    for (long i = 0; i < input_size; i++) {
        frequencies[input[i]]++;
    }
    
    // Build Huffman tree
    HuffmanNode* root = build_huffman_tree(frequencies);
    if (!root) return COMP_ERR_COMPRESSION_FAILED;
    
    // Generate Huffman codes
    char codes[256][256];
    memset(codes, 0, sizeof(codes));
    char current_code[256];
    generate_huffman_codes(root, codes, current_code, 0);
    
    // Create bit writer
    BitWriter* writer = create_bit_writer(input_size);
    
    // Write header: frequencies for reconstruction
    unsigned char* header = (unsigned char*)malloc(256 * 5); // char + 4-byte frequency
    int header_size = 0;
    
    for (int i = 0; i < 256; i++) {
        if (frequencies[i] > 0) {
            header[header_size++] = (unsigned char)i;
            header[header_size++] = (frequencies[i] >> 24) & 0xFF;
            header[header_size++] = (frequencies[i] >> 16) & 0xFF;
            header[header_size++] = (frequencies[i] >> 8) & 0xFF;
            header[header_size++] = frequencies[i] & 0xFF;
        }
    }
    
    // Write end marker
    header[header_size++] = 0xFF;
    header[header_size++] = 0xFF;
    header[header_size++] = 0xFF;
    header[header_size++] = 0xFF;
    header[header_size++] = 0xFF;
    
    // Encode the input using Huffman codes
    for (long i = 0; i < input_size; i++) {
        char* code = codes[input[i]];
        for (int j = 0; code[j]; j++) {
            write_bit(writer, code[j] - '0');
        }
    }
    
    flush_bits(writer);
    
    // Combine header and compressed data
    *output_size = header_size + writer->size;
    *output = (unsigned char*)malloc(*output_size);
    memcpy(*output, header, header_size);
    memcpy(*output + header_size, writer->buffer, writer->size);
    
    // Cleanup
    free(header);
    free(writer->buffer);
    free(writer);
    free_huffman_tree(root);
    
    return COMP_SUCCESS;
}

// Huffman decompression function
CompResult huffman_decompress(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    if (!input || input_size <= 0) return COMP_ERR_INVALID_PARAMS;
    
    // Read header to reconstruct frequency table
    unsigned int frequencies[256] = {0};
    int pos = 0;
    
    while (pos < input_size - 4) {
        if (input[pos] == 0xFF && input[pos+1] == 0xFF && 
            input[pos+2] == 0xFF && input[pos+3] == 0xFF && input[pos+4] == 0xFF) {
            pos += 5;
            break;
        }
        
        unsigned char ch = input[pos++];
        unsigned int freq = (input[pos] << 24) | (input[pos+1] << 16) | 
                           (input[pos+2] << 8) | input[pos+3];
        pos += 4;
        frequencies[ch] = freq;
    }
    
    // Rebuild Huffman tree
    HuffmanNode* root = build_huffman_tree(frequencies);
    if (!root) return COMP_ERR_DECOMPRESSION_FAILED;
    
    // Calculate original size
    long original_size = 0;
    for (int i = 0; i < 256; i++) {
        original_size += frequencies[i];
    }
    
    *output = (unsigned char*)malloc(original_size);
    *output_size = 0;
    
    // Decode the compressed data
    HuffmanNode* current = root;
    for (long i = pos; i < input_size && *output_size < original_size; i++) {
        unsigned char byte = input[i];
        for (int bit = 7; bit >= 0 && *output_size < original_size; bit--) {
            int bit_value = (byte >> bit) & 1;
            
            if (bit_value == 0) {
                current = current->left;
            } else {
                current = current->right;
            }
            
            // If we reach a leaf node
            if (!current->left && !current->right) {
                (*output)[(*output_size)++] = current->data;
                current = root;
            }
        }
    }
    
    free_huffman_tree(root);
    return COMP_SUCCESS;
}

// Free Huffman tree memory
void free_huffman_tree(HuffmanNode* root) {
    if (!root) return;

    free_huffman_tree(root->left);
    free_huffman_tree(root->right);
    free(root);
}

// Optimized Huffman decompression for hardcore pipeline
// This version uses tracked_malloc for memory management
CompResult huffman_decompress_optimized(const unsigned char* input, long input_size, unsigned char** output, long* output_size) {
    if (!input || input_size <= 0) return COMP_ERR_INVALID_PARAMS;

    // Read header to reconstruct frequency table
    unsigned int frequencies[256] = {0};
    int pos = 0;

    while (pos < input_size - 4) {
        if (input[pos] == 0xFF && input[pos+1] == 0xFF &&
            input[pos+2] == 0xFF && input[pos+3] == 0xFF && input[pos+4] == 0xFF) {
            pos += 5;
            break;
        }

        unsigned char ch = input[pos++];
        unsigned int freq = (input[pos] << 24) | (input[pos+1] << 16) |
                           (input[pos+2] << 8) | input[pos+3];
        pos += 4;
        frequencies[ch] = freq;
    }

    // Rebuild Huffman tree
    HuffmanNode* root = build_huffman_tree(frequencies);
    if (!root) return COMP_ERR_DECOMPRESSION_FAILED;

    // Calculate original size
    long original_size = 0;
    for (int i = 0; i < 256; i++) {
        original_size += frequencies[i];
    }

    *output = (unsigned char*)tracked_malloc(original_size);
    if (!*output) {
        free_huffman_tree(root);
        return COMP_ERR_MEMORY;
    }
    *output_size = 0;

    // Decode the compressed data
    HuffmanNode* current = root;
    for (long i = pos; i < input_size && *output_size < original_size; i++) {
        unsigned char byte = input[i];
        for (int bit = 7; bit >= 0 && *output_size < original_size; bit--) {
            int bit_value = (byte >> bit) & 1;

            if (bit_value == 0) {
                current = current->left;
            } else {
                current = current->right;
            }

            // If we reach a leaf node
            if (!current->left && !current->right) {
                (*output)[(*output_size)++] = current->data;
                current = root;
            }
        }
    }

    free_huffman_tree(root);
    return COMP_SUCCESS;
}