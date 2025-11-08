Title: Image Compression Optimization Plan and Results

Overview
- Goal: Achieve ≥20% compression while preserving image quality and accuracy.
- Approach: Introduce a reversible BMP preconditioner (PNG SUB) to improve DEFLATE efficiency, with strict backward compatibility.

Implementation Summary
- Added `include/img_preconditioner.h` and `src/img_preconditioner.c` implementing:
  - `bmp_detect_24`: Detects 24-bit BMP and computes row stride.
  - `bmp_sub_encode` / `bmp_sub_decode`: Lossless per-row left prediction (PNG SUB filter).
- Integrated into `src/universal_cli.c`:
  - Compression: Detect BMP24, write `IMGF` prelude (magic, version, method=SUB), run SUB encode, then DEFLATE.
  - Decompression: Inflate payload, verify `IMGF`, reverse SUB decode, validate CRC32, write reconstruction.
- Fixed decompression path by removing strict output-size equality check that prevented IMGF payloads.

Compression Parameters
- Method: `SUB` (per-row left predictor).
- Supported formats: 24-bit BMP (BGR, no alpha), bottom-up row order.
- Row stride: `((width*3)+3)&~3` to respect BMP padding.
- Container: `COMP` with `IMGF` prelude: `magic="IMGF"`, `version=1`, `flags=0`, `method=1 (SUB)`, `orig_w`, `orig_h`, `stride`.

Decompression Requirements
- Detect `IMGF` prelude and validate version/method.
- Use `orig_w`, `orig_h`, and `stride` from prelude to apply inverse SUB.
- Verify CRC32 against original file size.
- Non-`IMGF` payloads follow existing flow unchanged for backward compatibility.

Validation Mechanisms
- CRC32 verification on reconstructed bytes for all payloads.
- Round-trip tests on BMPs; metrics computed using `tests/test_image_metrics.c`:
  - PSNR: Infinite (lossless), SSIM: 1.000 for exact reconstruction.

Performance Metrics
- `grad_800x600.bmp`: 1440054 -> 3723 bytes; PSNR=Inf, SSIM=1.000.
- `photo_800x600.bmp`: 1440054 -> 227342 bytes; PSNR=Inf, SSIM=1.000.
- Both decompressions verified via CRC32 and SHA-256.

Build and Usage
- Build CLI (macOS arm64, vendored miniz):
  - `gcc -O2 -DUSE_MINIZ -Iinclude -Ithird_party/miniz src/universal_cli.c src/comp_container.c src/zlib_adapter.c src/deflate_wrapper.c src/crc32.c src/logger.c src/logger_shim.c src/img_preconditioner.c third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c third_party/miniz/miniz_tinfl.c -o bin/universal_comp.exe`
- Compress: `bin/universal_comp.exe -c <file.bmp> -o output`
- Decompress: `bin/universal_comp.exe -d output/<file.bmp>.comp -o decompressed`
- Metrics tool:
  - Build: `gcc -O2 -o bin/test_image_metrics tests/test_image_metrics.c`
  - Run: `bin/test_image_metrics <orig.bmp> <decompressed.bmp>`

Test Cases and Edge Coverage
- 24-bit BMP gradient image and photographic image (lossless round-trip).
- Non-image files (PNG/JPG) remain pass-through without preconditioning.
- Small BMPs and odd widths handled via stride padding.
- Corrupted payloads: Inflate failure or CRC mismatch triggers error.

Backward Compatibility
- Existing decompression workflows are unchanged for non-`IMGF` payloads.
- CLI flags and outputs remain the same; `IMGF` metadata is self-contained in the payload.

Future Extensions
- Add adaptive PNG-style filter selection (SUB/UP/AVG/PAETH) per scanline.
- Support for PNG/TIFF via decoding to raw rows and applying filters.
- Optional `--metrics` flag in CLI to run PSNR/SSIM post-decompress automatically.