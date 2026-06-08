# blind-watermark-wasm

[![npm version](https://img.shields.io/npm/v/blind-watermark-wasm.svg)](https://www.npmjs.com/package/blind-watermark-wasm)
[![npm downloads](https://img.shields.io/npm/dm/blind-watermark-wasm.svg)](https://www.npmjs.com/package/blind-watermark-wasm)
[![minzipped size](https://img.shields.io/bundlephobia/minzip/blind-watermark-wasm)](https://bundlephobia.com/package/blind-watermark-wasm)
[![types](https://img.shields.io/npm/types/blind-watermark-wasm.svg)](https://www.npmjs.com/package/blind-watermark-wasm)
[![license](https://img.shields.io/npm/l/blind-watermark-wasm.svg)](https://github.com/JackCaow/blind-watermark-wasm)

DWT-DCT-SVD based blind watermarking for images using WebAssembly. Works in browsers and Node.js/Electron environments.

**[🔗 Live demo](https://JackCaow.github.io/blind-watermark-wasm/)** &middot; **[📦 npm](https://www.npmjs.com/package/blind-watermark-wasm)** &middot; **[💻 Source](https://github.com/JackCaow/blind-watermark-wasm)**

## Features

- **Blind watermarking**: No original image needed for extraction
- **Robust algorithm**: DWT-DCT-SVD with redundancy for JPEG compression resistance
- **Cross-platform**: Works in browsers, Node.js, and Electron
- **Multiple formats**: Supports PNG, JPEG, and WebP
- **Transparency preserved**: RGBA inputs keep their alpha channel (PNG/WebP output; JPEG has no alpha and is flattened to RGB)
- **TypeScript support**: Full type definitions included

## Installation

```bash
npm install blind-watermark-wasm
```

The WebAssembly is **inlined** into the JS (single-file build), so it bundles with
**Vite, webpack, Next.js, esbuild, Rollup, etc. with zero configuration** — no
wasm loader, no asset/`publicDir` setup, no separate `.wasm` to serve. Just import
and bundle. (Trade-off: a larger JS bundle, since the wasm rides along base64-encoded.)

## Usage

### Recommended: self-describing watermark (no length to track)

`embed` stores the payload length inside the image, so `extract` reads it back
with **nothing but the image** — no `wmBitLength` to persist or pass around.

```typescript
import { BlindWatermark } from 'blind-watermark-wasm';
import fs from 'fs';

const bwm = new BlindWatermark();
const imageData = new Uint8Array(fs.readFileSync('image.png'));

// Embed (text or raw bytes)
const watermarked = await bwm.embed(imageData, 'Hello World', 'png');
fs.writeFileSync('watermarked.png', watermarked);

// Extract — no length needed
const result = await bwm.extract(watermarked);
if (result && result.valid) {
  console.log(result.text); // "Hello World"  (result.bytes for raw payloads)
}

// Just checking whether an image is watermarked?
const { found } = await bwm.detect(watermarked);
```

`extract` returns `null` when no watermark is found (wrong password or none
present), and `result.valid` is `false` if the payload checksum fails (e.g. heavy
re-compression). An input that can't be decoded as an image **throws** instead —
an invalid file is a failure, never silently reported as "no watermark". Embed raw
bytes by passing a `Uint8Array` instead of a string.

### Low-level: explicit-length API

`embedString`/`extractString` embed the bare payload and require you to keep the
returned `wmBitLength` to extract later. Prefer `embed`/`extract` above unless you
need to manage the bit length yourself.

```typescript
const { imageData: watermarked, wmBitLength } = await bwm.embedString(imageData, 'Hello World', 'png');
const text = await bwm.extractString(watermarked, wmBitLength); // wmBitLength required
```

### With Custom Config

```typescript
const bwm = new BlindWatermark({
  passwordWm: 12345,    // Watermark password
  passwordImg: 67890,   // Image password
  d1: 36,               // Primary quantization step
  d2: 20,               // Secondary quantization step
  redundancy: 3,        // Bit redundancy for robustness
});
```

### Convenience Functions

```typescript
import { embedStringWatermark, extractStringWatermark } from 'blind-watermark-wasm';

// Embed
const result = await embedStringWatermark(imageData, 'Secret Message');

// Extract
const text = await extractStringWatermark(result.imageData, result.wmBitLength);
```

### Browser Usage

```html
<script type="module">
  import { BlindWatermark } from 'blind-watermark-wasm';

  const bwm = new BlindWatermark();

  // Handle file input
  async function handleFile(file) {
    const arrayBuffer = await file.arrayBuffer();
    const imageData = new Uint8Array(arrayBuffer);

    const result = await bwm.embedString(imageData, 'Watermark', 'png');

    // Create download link
    const blob = new Blob([result.imageData], { type: 'image/png' });
    const url = URL.createObjectURL(blob);
    // ...
  }
</script>
```

### Electron Usage

```typescript
// In renderer process
import { BlindWatermark } from 'blind-watermark-wasm';

const bwm = new BlindWatermark();

// Works the same as Node.js
const result = await bwm.embedString(imageData, 'Watermark');
```

## API

### `BlindWatermark`

#### Constructor

```typescript
new BlindWatermark(config?: WatermarkConfig)
```

#### Methods

Self-describing (recommended — no length to track):

- `embed(imageData, data, format?)` - Embed text or `Uint8Array`; returns the watermarked image bytes
- `extract(imageData)` - Returns `ExtractResult | null` (`{ found, valid, version, isText, bytes, text? }`)
- `detect(imageData)` - Returns `DetectResult` (`{ found, valid, version, isText, byteLength }`)

Low-level (explicit length):

- `embedString(imageData, text, format?)` - Embed text watermark, returns `{ imageData, wmBitLength }`
- `extractString(imageData, wmBitLength)` - Extract text watermark (length required)
- `embedBinary(imageData, bits, format?)` - Embed binary data
- `extractBinary(imageData, wmBitLength)` - Extract binary data (length required)

### `WatermarkConfig`

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| passwordWm | number | 1 | Watermark scrambling password |
| passwordImg | number | 1 | Block selection password |
| d1 | number | 36 | Primary quantization step |
| d2 | number | 0 | Secondary quantization step (opt-in; see note) |
| blockSize | number | 4 | DCT block size |
| dwtLevel | number | 1 | DWT decomposition level |
| redundancy | number | 3 | Bit redundancy for robustness |

> **Note on `d2`:** The secondary singular value carries higher-frequency energy and
> is much more fragile under noise/JPEG than the primary one. Embedding into it
> (`d2 > 0`) measurably *lowers* extraction robustness, so it is disabled by default.
> Leave it at `0` unless you have a specific reason and have benchmarked your case.

## Robustness

Measured on a 512×512 test image, 41-byte payload, default config (run it yourself with
`bash script/run_bench.sh`). BER = bit-error rate of the recovered payload.

| Transform | Result | BER |
|-----------|--------|-----|
| PNG re-save (lossless) | recovered | 0% |
| JPEG q=95 … q=60 | recovered (CRC ok) | 0% |
| JPEG q=50 | corrupt (CRC catches it) | ~0.3% |
| JPEG q=40 | unreadable | — |
| Downscale 50% then upscale | unreadable | — |
| Resize to 90% | unreadable | — |

So the watermark reliably survives PNG re-saves and JPEG down to roughly **quality 60**,
and the CRC flags corruption instead of returning silently-wrong bytes. Numbers vary with
image content; benchmark your own assets.

## Limitations

- **Not resilient to geometric changes.** The block grid is locked to the image
  dimensions, so **resize, crop, and rotation destroy the watermark** — even resizing
  back to the original size. Embed *after* all resizing, and re-embed if you re-scale.
  This rules out the "post to a platform that re-scales everything, then verify" use case.
- **Not encryption.** The password only scrambles bit placement; the payload is embedded
  as plaintext bits. Encrypt the payload yourself before embedding if it is sensitive.
- **Not tamper-proof.** The CRC-16 detects accidental corruption, not forgery. For
  provenance you trust, sign the payload (e.g. HMAC) before embedding.
- **Image size cap.** Inputs above ~10 megapixels are rejected with a catchable error
  (the floating-point transforms would otherwise exhaust the 1 GB wasm heap).

## Building from Source

Requires Emscripten SDK.

```bash
# Install Emscripten
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# Build
cd blind-watermark-wasm
npm install
npm run build
```

## License

MIT
