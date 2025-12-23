# blind-watermark-wasm

DWT-DCT-SVD based blind watermarking for images using WebAssembly. Works in browsers and Node.js/Electron environments.

## Features

- **Blind watermarking**: No original image needed for extraction
- **Robust algorithm**: DWT-DCT-SVD with redundancy for JPEG compression resistance
- **Cross-platform**: Works in browsers, Node.js, and Electron
- **Multiple formats**: Supports PNG, JPEG, and WebP
- **TypeScript support**: Full type definitions included

## Installation

```bash
npm install blind-watermark-wasm
```

## Usage

### Basic Usage

```typescript
import { BlindWatermark } from 'blind-watermark-wasm';
import fs from 'fs';

// Create instance with default config
const bwm = new BlindWatermark();

// Read image
const imageData = new Uint8Array(fs.readFileSync('image.png'));

// Embed watermark
const { imageData: watermarkedImage, wmBitLength } = await bwm.embedString(
  imageData,
  'Hello World',
  'png'
);

// Save watermarked image
fs.writeFileSync('watermarked.png', watermarkedImage);

// Extract watermark
const extractedText = await bwm.extractString(watermarkedImage, wmBitLength);
console.log(extractedText); // "Hello World"
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

- `embedString(imageData, text, format?)` - Embed text watermark
- `extractString(imageData, wmBitLength)` - Extract text watermark
- `embedBinary(imageData, bits, format?)` - Embed binary data
- `extractBinary(imageData, wmBitLength)` - Extract binary data

### `WatermarkConfig`

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| passwordWm | number | 1 | Watermark scrambling password |
| passwordImg | number | 1 | Block selection password |
| d1 | number | 36 | Primary quantization step |
| d2 | number | 20 | Secondary quantization step |
| blockSize | number | 4 | DCT block size |
| dwtLevel | number | 1 | DWT decomposition level |
| redundancy | number | 3 | Bit redundancy for robustness |

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
