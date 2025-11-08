# Compression Algorithms Guide (Hinglish)

Yeh document aapko major compression algorithms ka clear, practical, aur developer-friendly overview deta hai. Har algorithm ke liye: definition, logic/flow, pseudo-code, example, use-cases, benefits, comparisons, practical applications, pitfalls, aur kab use karna chahiye — sab Hinglish mein.

---

## Quick Map

- Lossless basics: RLE, Huffman, LZ77, LZW, DEFLATE, LZ4
- Advanced + hybrid: LZMA, BWT+MTF+Huffman, Delta+RLE, PNG filters
- Audio (lossy): Psychoacoustic ideas for MP3/AAC-type compression
- Integrity: CRC32/Hashing for verify and corruption detection

---

## Using This Repo’s Tooling Quickly

- Compress: `./bin/universal_comp.exe -c <input_file>` → output `output/<file>.comp`
- Decompress: `./bin/universal_comp.exe -d output/<file>.comp` → `decompressed/<file>`
- Zlib-style: `./bin/universal_comp.exe --zlib -c <input_file>`
- Important policy: Decompressor ab sirf `output/` directory ke `.comp` files accept karta hai.

---

## 1) Huffman Coding

### Definition
Huffman Coding ek prefix-free variable-length coding technique hai jo frequent symbols ko chhote codes aur rare symbols ko lambe codes assign karta hai — optimal (near-entropy) coding for symbol frequencies.

### Logic / Flow
1. Frequency count per symbol
2. Min-heap banaao of leaf nodes (freq, symbol)
3. Two smallest nodes merge karke internal node banao; repeat till one tree
4. Left=0, Right=1 se prefix codes derive karo
5. Input ko bitstream me encode karo

### Pseudo-code (C-ish)
```c
struct Node { int freq; int sym; struct Node *l, *r; };
void build_freq(const uint8_t *in, size_t n, int freq[256]);
Node *build_tree(int freq[256]);
void derive_codes(Node *root, BitCode codes[256]);
void encode(const uint8_t *in, size_t n, BitCode codes[256], BitWriter *bw);
```

### Example
Text: `ABBCCCD` → Freq: A1 B2 C3 D1 → Huffman codes approx: C=0, B=10, A=110, D=111 → Encoded bits length kam ho jaata hai vs fixed 8-bit.

### Use Cases & Benefits
- General text/logs, headers, image palettes, metadata
- Simple, fast, memory-light; compression ratio decent when freq skewed

### Comparisons
- RLE se zyada powerful (random data pe bhi work karta)
- LZ-family ke sath combine karna best (DEFLATE mein Huffman + LZ77)

### Practical Applications
- PNG, DEFLATE, many codecs

### Pitfalls & Gotchas
- Uniform distribution pe benefit kam
- Tree overhead small—but very tiny blocks pe noticeable

### When to Use
- Jab symbol frequency skewed ho, or as backend coding for LZ outputs

---

## 2) LZ77

### Definition
LZ77 sliding-window compression hai; past window me matches dhundta hai (length, distance) tuples generate karta hai; unmatched symbols literals rehte hain.

### Logic / Flow
1. Maintain circular buffer (window)
2. Current position se longest match pahle ke data me find karo
3. Agar match mila: emit `(distance, length)`; varna emit literal
4. Window slide; repeat till end

### Pseudo-code
```c
while (pos < n) {
  Match m = find_longest_match(window, pos);
  if (m.length >= MIN_LEN) emit_copy(m.dist, m.length);
  else emit_literal(in[pos]);
  pos += max(1, m.length);
}
```

### Example
Input: `ABABABA` → `AB` repeat hota rahta; tuples save bits vs literals.

### Use Cases & Benefits
- Text, binaries, repetitive patterns
- Great with Huffman coding; streaming-friendly

### Comparisons
- LZW dictionary-based (static growth) vs LZ77 dynamic window
- DEFLATE is LZ77 + Huffman

### Practical Applications
- Gzip/Deflate, Zip, PNG backend

### Pitfalls
- Naive longest-match slow; use hash chains / suffix arrays
- Window size limits ratio

### When to Use
- General-purpose lossless, esp. with Huffman

---

## 3) LZW

### Definition
LZW dictionary grows dynamically; sequences ke codes emit karte hai; koi explicit back-references nahi.

### Logic / Flow
1. Init dictionary with single-byte symbols
2. Greedy longest sequence in dict
3. Emit code; dictionary me new entry add (prev + next-char)

### Pseudo-code
```c
dict = init_256();
P = first_symbol(in);
for (C in rest) {
  if (exists(P+C)) P = P+C; else { emit(code(P)); add(P+C); P = C; }
}
emit(code(P));
```

### Use Cases & Benefits
- Text, early fax, GIF
- Simple; no need explicit distances

### Comparisons
- Patent issues historical; today fine
- Modern codecs prefer LZ77 + Huffman

### Practical
- GIF uses LZW

### Pitfalls
- Dictionary blow-up; need resets/limits

### When to Use
- Legacy formats, educational, constrained environments

---

## 4) RLE (Run-Length Encoding)

### Definition
Consecutive same-symbol runs ko `(symbol, count)` form me store karta.

### Logic
- Scan; count consecutive repeats; emit pair

### Pseudo-code
```c
int i=0;
while (i<n) {
  int j=i+1; while (j<n && in[j]==in[i]) j++;
  emit(in[i], j-i);
  i=j;
}
```

### Use Cases & Benefits
- Images with flats, masks, zeros, sparse data
- Super fast, trivial

### Comparisons
- Standalone weak; as pre/post stage strong

### Practical
- Fax, simple bitmap compression, zero-compression in storage

### Pitfalls
- Random data pe blow-up risk; use escape and thresholds

### When to Use
- High redundancy runs, masks, zero padding

---

## 5) DEFLATE (LZ77 + Huffman)

### Definition
Industry-standard (gzip/zip). LZ77 finds backreferences, Huffman encodes tokens.

### Logic
1. Tokenize literals, lengths, distances
2. Build dynamic/fixed Huffman trees
3. Encode stream blocks

### Pseudo-code (high-level)
```c
while (has_data) {
  tokens = lz77_tokenize(block);
  trees = build_huffman(tokens);
  write_trees(trees);
  huffman_encode(tokens, trees);
}
```

### Use Cases & Benefits
- General-purpose; great balance of speed/ratio

### Comparisons
- Slower than LZ4 but better ratio; faster than LZMA but lower ratio

### Practical
- Web assets gzip, PNG, ZIP

### Pitfalls
- Small blocks overhead; tune block sizes

### When to Use
- Default choice for portable lossless compression

---

## 6) LZMA

### Definition
Advanced LZ (range coding, large dictionary, sophisticated modeling). High ratios.

### Logic
- Big dict matches, literal modeling, range coder entropy

### Example
- 7z archives

### Use Cases & Benefits
- Binaries, large text; excellent compression ratio

### Comparisons
- Slower; more memory than DEFLATE/LZ4

### Practical
- Installer packages, archival

### Pitfalls
- High memory; slow decompression for low-end devices

### When to Use
- Max ratio needed, offline/archival settings

---

## 7) BWT + MTF + Huffman

### Definition
Burrows–Wheeler Transform reorders data to cluster similar characters, then Move-To-Front + Huffman compress easily.

### Logic
1. BWT: all rotations sort, last column emit
2. MTF: symbols become small integers
3. Huffman: code integers

### Pseudo-code
```c
bwt_out = bwt_transform(in);
mtf_out = mtf_encode(bwt_out);
huff_out = huffman_encode(mtf_out);
```

### Use Cases & Benefits
- Text with patterns; strong ratios

### Comparisons
- Good vs DEFLATE on certain corpora

### Practical
- bzip2 style

### Pitfalls
- BWT heavy on memory/time; blockwise tuning needed

### When to Use
- Archival, when BWT suitable for data patterns

---

## 8) Delta + RLE

### Definition
First difference (delta) reduces entropy for slowly changing signals; RLE compresses long runs.

### Logic
1. Delta: `out[i] = in[i] - in[i-1]`
2. RLE on `out`

### Pseudo-code
```c
prev = 0; for (i=0;i<n;i++){ d = in[i]-prev; prev=in[i]; emit(d);} // then RLE
```

### Use Cases & Benefits
- Images (scanlines), time-series, PCM audio deltas

### Comparisons
- As prefilter before entropy coding

### Practical
- PNG filters internally apply delta-like filters

### Pitfalls
- Overflow/underflow; choose signed width carefully

### When to Use
- Gradual transitions and plateaus

---

## 9) PNG Filter SUB (example)

### Definition
PNG per-scanline filters (SUB/UP/AVG/PAETH) data ko decorrelate karte hain before DEFLATE.

### Logic (SUB)
- Each byte minus left neighbor

### Pseudo-code
```c
for (x=0;x<width;x++) out[x] = in[x] - in[x-1];
```

### Use Cases & Benefits
- Images: improves DEFLATE efficiency

### Comparisons
- Different filters suit different images (noise vs gradients)

### Practical
- PNG encoders try multiple filters per line

### Pitfalls
- Wrong filter selection hurts ratio

### When to Use
- Lossless image pipelines

---

## 10) Psychoacoustic Audio Compression (Lossy)

### Definition
Human hearing model use karke masking apply hota — inaudible parts ko discard/quantize; MP3/AAC types.

### Logic
1. Transform (FFT/MDCT), critical bands
2. Masking thresholds; bit allocation
3. Quantization + entropy coding

### Use Cases & Benefits
- Huge size savings for audio; acceptable perceived quality

### Comparisons
- Lossy; not suitable for archival

### Practical
- Streaming, music distribution

### Pitfalls
- Artifacts at low bitrates; pre-echo issues

### When to Use
- When perceptual quality sufficient, not exact match required

---

## 11) CRC32 & Hashing

### Definition
Integrity check for streams/files; corruption detect karne ke liye.

### Logic
- Rolling polynomial mod 2; fast updates

### Pseudo-code
```c
uint32_t crc=~0u; for (b in in) crc = update_crc32(crc, b); crc ^= ~0u;
```

### Use Cases & Benefits
- Verify after decompression; network packets; archives

### Comparisons
- CRC32 fast but not cryptographic; use SHA-256 for stronger integrity

### Practical
- Zip and PNG footers, our tool’s status reporting

### Pitfalls
- Not secure against intentional tampering

### When to Use
- Routine integrity, not security-critical verification

---

## 12) LZ4

### Definition
Very fast LZ; low latency, modest ratio.

### Logic
- Hash matches, short sequences; speed-focused

### Use Cases & Benefits
- Real-time logs, in-memory compression, DB pages

### Comparisons
- Much faster than DEFLATE; lower ratio

### Practical
- DBs, game engines, streaming systems

### Pitfalls
- Weak on highly redundant data compared to DEFLATE/LZMA

### When to Use
- Speed-critical pipelines

---

## Integration Notes (Is Repo Ke Context Mein)

- CLI Usage:
  - Compress: `./bin/universal_comp.exe -c path/to/file`
  - Decompress: `./bin/universal_comp.exe -d output/file.ext.comp`
  - Zlib mode: `./bin/universal_comp.exe --zlib -c path/to/file`
- Directory policy:
  - Decompression strictly `output/*.comp` files par hi allow hai (security/consistency guard).
- Formats:
  - Images: Lossless filters + DEFLATE backend (PNG-like)
  - Text/JSON/XML: DEFLATE/LZ77+Huffman suitable
  - Audio: Lossless stub or external codecs for lossy

### Practical Examples
- PDF compress: `./bin/universal_comp.exe -c "Pink and Purple 3D Project Presentation (1).pdf"`
- Decompress: `./bin/universal_comp.exe -d "output/Pink and Purple 3D Project Presentation (1).pdf.comp"`
- Batch idea: script loop over `output/*.comp` and pass to `-d`

---

## Comparisons Summary

- Speed: LZ4 > DEFLATE > LZMA
- Ratio: LZMA > BWT+MTF+Huffman ≈ DEFLATE > LZ4
- Simplicity: RLE, Huffman easiest
- General-purpose default: DEFLATE (balance)

---

## Pitfalls & Best Practices

- Block sizes tune karein; too small ⇒ overhead, too big ⇒ memory/time
- Pre-filters (delta/PNG filters) try karein for better ratio on images
- Integrity: store CRC/SHA; verify post-decompression
- Escape mechanisms: RLE blow-up avoid
- Window/dict sizes: LZ-family tuning impacts ratio & speed

---

## When To Use What (Cheat Sheet)

- Logs/Text: DEFLATE; speed-critical ⇒ LZ4
- Images (lossless): PNG-style filters + DEFLATE
- Archival (max ratio): LZMA or BWT-based
- Simple runs/masks: RLE or Delta+RLE
- Audio streaming: Psychoacoustic (MP3/AAC), not in this repo lossless-only

---

## FAQ

- Q: `.comp` kahaan se decompress hoga?
  - A: Sirf `output/` folder ke `.comp` files accepted hain; result `decompressed/` me.
- Q: Spaces in filename?
  - A: Zsh me quotes use karein: `"My File (1).pdf"`
- Q: Verification?
  - A: SHA-256/CRC32 compare karein original vs decompressed (lossless cases).

---

## References

- DEFLATE (RFC 1951), GZIP
- PNG (RFC 2083) — filters + DEFLATE
- LZ4, LZMA docs (7zip)
- BWT original paper, bzip2
- Psychoacoustics — MP3/AAC overview papers

---

## Notes

- Yeh document practical developer ke liye bana hai — aap easily isko follow karke apne compression pipeline ko tune/choose kar sakte hain.
- Repo-specific guard: Decompression restriction `output/*.comp` only — security + reproducibility.

