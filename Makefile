# File Compressor Makefile
# PBL Project - DSA Implementation

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
LDFLAGS =
DEBUG_FLAGS = -g -DDEBUG
INCLUDE_DIR = include
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Target executable
TARGET = $(BIN_DIR)/file_compressor.exe

# Source files
SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/huffman.c \
          $(SRC_DIR)/lz77.c \
          $(SRC_DIR)/utils.c \
          $(SRC_DIR)/compressor.c \
          $(SRC_DIR)/delta_rle.c \
          $(SRC_DIR)/bwt.c \
          $(SRC_DIR)/hardcore_compression.c \
          $(SRC_DIR)/audio_compressor.c \
          $(SRC_DIR)/gui.c \
          missing_functions.c

# Object files
OBJS = $(OBJ_DIR)/main.o $(OBJ_DIR)/huffman.o $(OBJ_DIR)/lz77.o $(OBJ_DIR)/compressor.o \
       $(OBJ_DIR)/delta_rle.o \
       $(OBJ_DIR)/hardcore_compression.o $(OBJ_DIR)/bwt.o $(OBJ_DIR)/bwt_mtf_huffman.o \
       $(OBJ_DIR)/audio_compressor.o $(OBJ_DIR)/image_compressor_stub.o $(OBJ_DIR)/lzw_compress_stub.o \
       $(OBJ_DIR)/utils.o $(OBJ_DIR)/parser.o $(OBJ_DIR)/missing_functions.o \
       $(OBJ_DIR)/bitio.o $(OBJ_DIR)/crc32.o $(OBJ_DIR)/batch_decompressor.o \
       $(OBJ_DIR)/file_analyzer.o $(OBJ_DIR)/comprehensive_tester.o \
       $(OBJ_DIR)/pdf_reflate.o

# Optional zlib integration (set ZLIB_ENABLED=1 to enable)
ZLIB_ENABLED ?= 0
ifeq ($(ZLIB_ENABLED),1)
    CFLAGS += -DUSE_ZLIB
    LDFLAGS += -lz
endif

# Optional miniz integration (vendored) (set MINIZ_ENABLED=1 to enable)
MINIZ_ENABLED ?= 1
ifeq ($(MINIZ_ENABLED),1)
    CFLAGS += -DUSE_MINIZ -Ithird_party/miniz -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZLIB_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
    # Build required miniz compilation units (tdefl/tinfl) and our deflate wrapper
    MINIZ_OBJ := $(OBJ_DIR)/miniz.o $(OBJ_DIR)/miniz_tdef.o $(OBJ_DIR)/miniz_tinfl.o
    MINIZ_SRC := third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c third_party/miniz/miniz_tinfl.c
    # Include deflate wrapper and miniz objs in main build when enabled
    OBJS += $(OBJ_DIR)/deflate_wrapper.o
else
    MINIZ_OBJ :=
    MINIZ_SRC :=
endif

# Optional LZMA integration (set LZMA_ENABLED=1 to enable; requires LZMA SDK linked)
LZMA_ENABLED ?= 0
ifeq ($(LZMA_ENABLED),1)
    CFLAGS += -DHAVE_LZMA
    LZMA_OBJ := $(OBJ_DIR)/lzma_wrapper.o $(OBJ_DIR)/lzma_sdk_stub.o
else
    LZMA_OBJ :=
endif

# Mutual-exclusion guard: zlib and miniz cannot both be enabled
.PHONY: guard
GUARD_ERR := $(if $(filter 1,$(ZLIB_ENABLED)),$(if $(filter 1,$(MINIZ_ENABLED)),$(error ZLIB_ENABLED=1 and MINIZ_ENABLED=1 cannot be used simultaneously),),)
guard:
	@$(GUARD_ERR)
	@echo Guard check passed: ZLIB_ENABLED=$(ZLIB_ENABLED), MINIZ_ENABLED=$(MINIZ_ENABLED)

# Default target
.PHONY: all cli
all: directories $(BIN_DIR)/file_compressor.exe $(BIN_DIR)/decompress_one.exe $(BIN_DIR)/test_suite.exe $(BIN_DIR)/roundtrip.exe $(BIN_DIR)/simple_test.exe $(BIN_DIR)/batch_analyzer.exe $(BIN_DIR)/working_batch_analyzer.exe $(BIN_DIR)/universal_decompressor.exe $(BIN_DIR)/production_tester.exe $(BIN_DIR)/universal_comp.exe

# Minimal target to build only the universal compressor CLI (used by Docker)
cli: directories $(BIN_DIR)/universal_comp.exe
	@echo Built $(BIN_DIR)/universal_comp.exe

$(BIN_DIR)/working_batch_analyzer.exe: $(OBJ_DIR)/working_batch_analyzer.o $(OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/working_batch_analyzer.exe $(OBJ_DIR)/working_batch_analyzer.o $(OBJS) $(LDFLAGS)

$(BIN_DIR)/universal_decompressor.exe: $(OBJ_DIR)/universal_decompressor.o $(OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/universal_decompressor.exe $(OBJ_DIR)/universal_decompressor.o $(OBJS) $(LDFLAGS)

$(BIN_DIR)/production_tester.exe: $(OBJ_DIR)/production_tester.o $(OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/production_tester.exe $(OBJ_DIR)/production_tester.o $(OBJS) $(LDFLAGS)

# Build universal compressor CLI
# Ensure required directories exist when directly invoking this target (e.g., Docker build)
$(BIN_DIR)/universal_comp.exe: directories $(OBJ_DIR)/universal_cli.o $(OBJ_DIR)/comp_container.o $(OBJ_DIR)/zlib_adapter.o $(OBJ_DIR)/crc32.o $(OBJ_DIR)/deflate_wrapper.o $(OBJ_DIR)/logger.o $(OBJ_DIR)/logger_shim.o $(OBJ_DIR)/image_compressor_stub.o $(MINIZ_OBJ) $(LZMA_OBJ)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/universal_comp.exe $(OBJ_DIR)/universal_cli.o $(OBJ_DIR)/comp_container.o $(OBJ_DIR)/zlib_adapter.o $(OBJ_DIR)/crc32.o $(OBJ_DIR)/deflate_wrapper.o $(OBJ_DIR)/logger.o $(OBJ_DIR)/logger_shim.o $(OBJ_DIR)/image_compressor_stub.o $(MINIZ_OBJ) $(LZMA_OBJ) $(LDFLAGS)

# Realtime target: high-optimization build with optional liburing on Linux
UNAME_S := $(shell uname -s 2>/dev/null)
REALTIME_CFLAGS := -O3 -march=native -flto -DNDEBUG
REALTIME_LDFLAGS :=
ifeq ($(UNAME_S),Linux)
    REALTIME_LDFLAGS += -luring -pthread
endif

.PHONY: realtime
realtime: directories $(BIN_DIR)/universal_comp_rt.exe $(BIN_DIR)/realtime_server

$(BIN_DIR)/universal_comp_rt.exe:
	$(CC) $(REALTIME_CFLAGS) -I$(INCLUDE_DIR) $(SRC_DIR)/universal_cli.c $(SRC_DIR)/comp_container.c $(SRC_DIR)/zlib_adapter.c $(SRC_DIR)/crc32.c $(MINIZ_SRC) -o $(BIN_DIR)/universal_comp_rt.exe $(LDFLAGS) $(REALTIME_LDFLAGS)

# Build realtime clone-and-compress server (Linux-only)
ifeq ($(UNAME_S),Linux)
$(BIN_DIR)/realtime_server: $(SRC_DIR)/realtime_server.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(REALTIME_CFLAGS) -I$(INCLUDE_DIR) \
		$(SRC_DIR)/realtime_server.c \
		$(SRC_DIR)/compressor.c $(SRC_DIR)/delta_rle.c $(SRC_DIR)/utils.c $(SRC_DIR)/hardcore_compression.c \
		$(SRC_DIR)/huffman.c $(SRC_DIR)/lz77.c $(SRC_DIR)/bwt_mtf_huffman.c $(SRC_DIR)/crc32.c $(SRC_DIR)/missing_functions.c \
		-o $(BIN_DIR)/realtime_server $(LDFLAGS) $(REALTIME_LDFLAGS)
endif

# Create necessary directories
directories:
	@mkdir -p "$(OBJ_DIR)"
	@mkdir -p "$(BIN_DIR)"

# Build the main executable
$(TARGET): $(OBJS) $(MINIZ_OBJ) $(LZMA_OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(MINIZ_OBJ) $(LZMA_OBJ) $(LDFLAGS)
	@echo.
	@echo ===================================
	@echo   File Compressor Built Successfully!
	@echo ===================================
	@echo Executable: $(TARGET)
	@echo.

# Compile source files to object files
$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/main.c -o $(OBJ_DIR)/main.o

$(OBJ_DIR)/huffman.o: $(SRC_DIR)/huffman.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/huffman.c -o $(OBJ_DIR)/huffman.o

$(OBJ_DIR)/lz77.o: $(SRC_DIR)/lz77.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/lz77.c -o $(OBJ_DIR)/lz77.o

$(OBJ_DIR)/utils.o: $(SRC_DIR)/utils.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/utils.c -o $(OBJ_DIR)/utils.o

$(OBJ_DIR)/compressor.o: $(SRC_DIR)/compressor.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/compressor.c -o $(OBJ_DIR)/compressor.o

$(OBJ_DIR)/delta_rle.o: $(SRC_DIR)/delta_rle.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/delta_rle.c -o $(OBJ_DIR)/delta_rle.o

$(OBJ_DIR)/bwt.o: $(SRC_DIR)/bwt.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/bwt.c -o $(OBJ_DIR)/bwt.o

$(OBJ_DIR)/bwt_mtf_huffman.o: $(SRC_DIR)/bwt_mtf_huffman.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/bwt_mtf_huffman.c -o $(OBJ_DIR)/bwt_mtf_huffman.o

$(OBJ_DIR)/audio_compressor.o: $(SRC_DIR)/audio_compressor.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/audio_compressor.c -o $(OBJ_DIR)/audio_compressor.o

$(OBJ_DIR)/image_compressor_stub.o: $(SRC_DIR)/image_compressor_stub.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-but-set-variable -I$(INCLUDE_DIR) -Ithird_party/miniz -c $(SRC_DIR)/image_compressor_stub.c -o $(OBJ_DIR)/image_compressor_stub.o

$(OBJ_DIR)/lzw_compress_stub.o: $(SRC_DIR)/lzw_compress_stub.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/lzw_compress_stub.c -o $(OBJ_DIR)/lzw_compress_stub.o

$(OBJ_DIR)/parser.o: $(SRC_DIR)/parser.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/parser.c -o $(OBJ_DIR)/parser.o

$(OBJ_DIR)/hardcore_compression.o: $(SRC_DIR)/hardcore_compression.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/hardcore_compression.c -o $(OBJ_DIR)/hardcore_compression.o

$(OBJ_DIR)/missing_functions.o: $(SRC_DIR)/missing_functions.c $(INCLUDE_DIR)/compressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/missing_functions.c -o $(OBJ_DIR)/missing_functions.o

$(OBJ_DIR)/bitio.o: $(SRC_DIR)/bitio.c $(INCLUDE_DIR)/decompressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/bitio.c -o $(OBJ_DIR)/bitio.o

$(OBJ_DIR)/crc32.o: $(SRC_DIR)/crc32.c $(INCLUDE_DIR)/decompressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/crc32.c -o $(OBJ_DIR)/crc32.o

$(OBJ_DIR)/batch_decompressor.o: $(SRC_DIR)/batch_decompressor.c $(INCLUDE_DIR)/decompressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/batch_decompressor.c -o $(OBJ_DIR)/batch_decompressor.o

$(OBJ_DIR)/universal_decompressor.o: $(SRC_DIR)/universal_decompressor.c $(INCLUDE_DIR)/decompressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/universal_decompressor.c -o $(OBJ_DIR)/universal_decompressor.o

$(OBJ_DIR)/file_analyzer.o: $(SRC_DIR)/file_analyzer.c $(INCLUDE_DIR)/decompressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/file_analyzer.c -o $(OBJ_DIR)/file_analyzer.o

$(OBJ_DIR)/comprehensive_tester.o: $(SRC_DIR)/comprehensive_tester.c $(INCLUDE_DIR)/decompressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/comprehensive_tester.c -o $(OBJ_DIR)/comprehensive_tester.o

$(OBJ_DIR)/production_tester.o: $(SRC_DIR)/production_tester.c $(INCLUDE_DIR)/decompressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/production_tester.c -o $(OBJ_DIR)/production_tester.o

# New modules for universal compressor
$(OBJ_DIR)/comp_container.o: $(SRC_DIR)/comp_container.c $(INCLUDE_DIR)/comp_container.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/comp_container.c -o $(OBJ_DIR)/comp_container.o

$(OBJ_DIR)/zlib_adapter.o: $(SRC_DIR)/zlib_adapter.c $(INCLUDE_DIR)/zlib_adapter.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/zlib_adapter.c -o $(OBJ_DIR)/zlib_adapter.o

$(OBJ_DIR)/universal_cli.o: $(SRC_DIR)/universal_cli.c $(INCLUDE_DIR)/comp_container.h $(INCLUDE_DIR)/zlib_adapter.h
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-unused-function -Wno-unused-but-set-variable -I$(INCLUDE_DIR) -c $(SRC_DIR)/universal_cli.c -o $(OBJ_DIR)/universal_cli.o

# Vendored miniz object
$(OBJ_DIR)/miniz.o: third_party/miniz/miniz.c third_party/miniz/miniz.h
	$(CC) $(CFLAGS) -Wno-unused-function -I$(INCLUDE_DIR) -Ithird_party/miniz -c third_party/miniz/miniz.c -o $(OBJ_DIR)/miniz.o

$(OBJ_DIR)/miniz_tdef.o: third_party/miniz/miniz_tdef.c third_party/miniz/miniz_tdef.h third_party/miniz/miniz_common.h
	$(CC) $(CFLAGS) -Wno-unused-function -I$(INCLUDE_DIR) -Ithird_party/miniz -c third_party/miniz/miniz_tdef.c -o $(OBJ_DIR)/miniz_tdef.o

$(OBJ_DIR)/miniz_tinfl.o: third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_tinfl.h third_party/miniz/miniz_common.h
	$(CC) $(CFLAGS) -Wno-unused-function -I$(INCLUDE_DIR) -Ithird_party/miniz -c third_party/miniz/miniz_tinfl.c -o $(OBJ_DIR)/miniz_tinfl.o

$(OBJ_DIR)/deflate_wrapper.o: $(SRC_DIR)/deflate_wrapper.c
	$(CC) $(CFLAGS) -Wno-unused-function -I$(INCLUDE_DIR) -Ithird_party/miniz -c $(SRC_DIR)/deflate_wrapper.c -o $(OBJ_DIR)/deflate_wrapper.o

$(OBJ_DIR)/lzma_wrapper.o: $(SRC_DIR)/lzma_wrapper.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/lzma_wrapper.c -o $(OBJ_DIR)/lzma_wrapper.o

$(OBJ_DIR)/lzma_sdk_stub.o: $(SRC_DIR)/lzma_sdk_stub.c third_party/miniz/miniz.h
	$(CC) $(CFLAGS) -Wno-unused-function -I$(INCLUDE_DIR) -Ithird_party/miniz -c $(SRC_DIR)/lzma_sdk_stub.c -o $(OBJ_DIR)/lzma_sdk_stub.o

$(OBJ_DIR)/logger.o: $(SRC_DIR)/logger.c $(INCLUDE_DIR)/decompressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/logger.c -o $(OBJ_DIR)/logger.o

$(OBJ_DIR)/logger_shim.o: $(SRC_DIR)/logger_shim.c $(INCLUDE_DIR)/decompressor.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/logger_shim.c -o $(OBJ_DIR)/logger_shim.o

# Debug build
debug: CFLAGS += $(DEBUG_FLAGS)
debug: directories $(TARGET)

# Clean build files
clean:
	@rm -rf "$(OBJ_DIR)" "$(BIN_DIR)"
	@echo Build files cleaned.

# Configure target: run guard and generate compile_commands.json
.PHONY: configure gen-compile-commands
configure: guard directories gen-compile-commands

gen-compile-commands:
	@echo Generating compile_commands.json...
	@bash -c 'if command -v bear >/dev/null 2>&1; then \
		bear --output compile_commands.json -- $(MAKE) -B all; \
	elif command -v compiledb >/dev/null 2>&1; then \
		compiledb make -B all; \
	else \
		echo "[]" > compile_commands.json; \
		echo "Warning: bear/compiledb not found. Generated empty compile_commands.json."; \
	fi' || (echo Failed to generate compile_commands.json && exit 1)

# Run the program
run: $(TARGET)
	@echo.
	@echo Running File Compressor...
	@echo.
	$(TARGET)

# Create sample test files
test-files:
	@echo Creating sample test files...
	@echo This is a sample text file for testing compression algorithms. > data/sample.txt
	@echo It contains some repeated text to demonstrate compression efficiency. >> data/sample.txt
	@echo The quick brown fox jumps over the lazy dog. >> data/sample.txt
	@echo The quick brown fox jumps over the lazy dog. >> data/sample.txt
	@echo The quick brown fox jumps over the lazy dog. >> data/sample.txt
	@echo {"name": "test", "value": 123, "array": [1, 2, 3, 4, 5]} > data/sample.json
	@echo ^<?xml version="1.0"?^>^<root^>^<item^>Test^</item^>^</root^> > data/sample.xml
	@echo Name,Age,City > data/sample.csv
	@echo John,25,New York >> data/sample.csv
	@echo Jane,30,Los Angeles >> data/sample.csv
	@echo Bob,35,Chicago >> data/sample.csv
	@echo Sample test files created in data/ folder.

# Install (copy to system path - optional)
install: $(TARGET)
	@echo.
	@echo To install system-wide, copy $(TARGET) to a directory in your PATH
	@echo.

# Help
help:
	@echo.
	@echo File Compressor - Available Make Targets:
	@echo ========================================
	@echo.
	@echo   all         - Build the project (default)
	@echo   debug       - Build with debug symbols
	@echo   clean       - Remove build files
	@echo   run         - Build and run the program
	@echo   test-files  - Create sample test files
	@echo   install     - Installation instructions
	@echo   help        - Show this help message
	@echo.
	@echo Usage Examples:
	@echo   make              # Build the project
	@echo   make run          # Build and run
	@echo   make test-files   # Create test files
	@echo   make clean        # Clean build
	@echo.

.PHONY: all clean run debug test-files install help directories
.PHONY: guard realtime configure gen-compile-commands
$(OBJ_DIR)/pdf_reflate.o: $(SRC_DIR)/pdf_reflate.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $(SRC_DIR)/pdf_reflate.c -o $(OBJ_DIR)/pdf_reflate.o