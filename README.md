# blind-watermark-wasm

DWT-DCT-SVD based blind watermarking for images using WebAssembly. Works in browsers and Node.js/Electron environments.

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
re-compression). Embed raw bytes by passing a `Uint8Array` instead of a string.

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
