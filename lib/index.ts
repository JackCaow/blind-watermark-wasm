/**
 * Blind Watermark WASM - DWT-DCT-SVD based blind watermarking
 * Supports embedding and extracting invisible watermarks in images
 */

// @ts-ignore - WASM module import
import BlindWatermarkModuleFactory from './blind_watermark.js';

export interface WatermarkConfig {
  passwordWm?: number;
  passwordImg?: number;
  d1?: number;
  d2?: number;
  blockSize?: number;
  dwtLevel?: number;
  redundancy?: number;
}

export interface EmbedResult {
  imageData: Uint8Array;
  wmBitLength: number;
}

export type OutputFormat = 'png' | 'jpg' | 'jpeg' | 'webp';

/** Result of a self-describing extraction (no payload length needed). */
export interface ExtractResult {
  /** A watermark with the matching password was detected (header magic ok). */
  found: boolean;
  /** Payload checksum (CRC-16) matched — the bytes are trustworthy. */
  valid: boolean;
  /** Payload format version from the header. */
  version: number;
  /** Payload was embedded as UTF-8 text. */
  isText: boolean;
  /** Raw payload bytes. */
  bytes: Uint8Array;
  /** Decoded UTF-8 text, present when `isText`. */
  text?: string;
}

/** Lightweight presence check result. */
export interface DetectResult {
  found: boolean;
  valid: boolean;
  version: number;
  isText: boolean;
  byteLength: number;
}

interface WasmModule {
  setConfig: (
    passwordWm: number,
    passwordImg: number,
    d1: number,
    d2: number,
    blockSize: number,
    dwtLevel: number,
    redundancy: number
  ) => void;
  embedStringWatermark: (imageData: Uint8Array, watermarkText: string, outputFormat: string) => any;
  extractStringWatermark: (imageData: Uint8Array, wmBitLength: number) => string;
  embedBinaryWatermark: (imageData: Uint8Array, watermarkBits: string, outputFormat: string) => any;
  extractBinaryWatermark: (imageData: Uint8Array, wmBitLength: number) => string;
  embedSelfDescribing: (imageData: Uint8Array, payload: Uint8Array, isText: boolean, outputFormat: string) => any;
  extractSelfDescribing: (imageData: Uint8Array) => any;
}

let wasmModule: WasmModule | null = null;
let initPromise: Promise<WasmModule> | null = null as Promise<WasmModule> | null;

const defaultConfig: Required<WatermarkConfig> = {
  passwordWm: 1,
  passwordImg: 1,
  d1: 36,
  d2: 0, // secondary singular value off by default — see note in README (lowers robustness)
  blockSize: 4,
  dwtLevel: 1,
  redundancy: 3,
};

/**
 * Initialize the WASM module
 */
async function init(): Promise<WasmModule> {
  if (wasmModule) return wasmModule;

  if (!initPromise) {
    initPromise = BlindWatermarkModuleFactory().then((module: WasmModule) => {
      wasmModule = module;
      return module;
    });
  }

  return initPromise!;
}

/**
 * BlindWatermark class for embedding and extracting watermarks
 */
export class BlindWatermark {
  private config: Required<WatermarkConfig>;

  constructor(config: WatermarkConfig = {}) {
    this.config = { ...defaultConfig, ...config };
  }

  private applyConfig(module: WasmModule): void {
    module.setConfig(
      this.config.passwordWm,
      this.config.passwordImg,
      this.config.d1,
      this.config.d2,
      this.config.blockSize,
      this.config.dwtLevel,
      this.config.redundancy
    );
  }

  /**
   * Embed a string watermark into an image
   * @param imageData - Image data as Uint8Array (PNG, JPEG, or WebP)
   * @param watermarkText - Text to embed
   * @param format - Output format: 'png', 'jpg', or 'webp'
   * @returns Embedded image data and watermark bit length
   */
  async embedString(
    imageData: Uint8Array,
    watermarkText: string,
    format: 'png' | 'jpg' | 'jpeg' | 'webp' = 'png'
  ): Promise<EmbedResult> {
    const module = await init();

    this.applyConfig(module);

    // Call WASM function
    const result = module.embedStringWatermark(imageData, watermarkText, format);

    if (result.error) {
      throw new Error(result.error);
    }

    return {
      imageData: new Uint8Array(result.imageData),
      wmBitLength: result.wmBitLength,
    };
  }

  /**
   * Extract a string watermark from an image
   * @param imageData - Image data as Uint8Array
   * @param wmBitLength - Length of watermark in bits (from embed result)
   * @returns Extracted watermark text
   */
  async extractString(imageData: Uint8Array, wmBitLength: number): Promise<string> {
    const module = await init();

    this.applyConfig(module);

    // Call WASM function
    return module.extractStringWatermark(imageData, wmBitLength);
  }

  /**
   * Embed binary watermark data into an image
   * @param imageData - Image data as Uint8Array
   * @param bits - Binary data as string of '0' and '1'
   * @param format - Output format
   * @returns Embedded image data and watermark bit length
   */
  async embedBinary(
    imageData: Uint8Array,
    bits: string,
    format: 'png' | 'jpg' | 'jpeg' | 'webp' = 'png'
  ): Promise<EmbedResult> {
    const module = await init();

    this.applyConfig(module);

    // Call WASM function
    const result = module.embedBinaryWatermark(imageData, bits, format);

    if (result.error) {
      throw new Error(result.error);
    }

    return {
      imageData: new Uint8Array(result.imageData),
      wmBitLength: result.wmBitLength,
    };
  }

  /**
   * Extract binary watermark from an image
   * @param imageData - Image data as Uint8Array
   * @param wmBitLength - Length of watermark in bits
   * @returns Binary data as string of '0' and '1'
   */
  async extractBinary(imageData: Uint8Array, wmBitLength: number): Promise<string> {
    const module = await init();

    this.applyConfig(module);

    // Call WASM function
    return module.extractBinaryWatermark(imageData, wmBitLength);
  }

  /**
   * Embed a self-describing watermark — recommended. The payload length is stored
   * in the image, so {@link extract} needs no extra information to read it back.
   * @param imageData - Image data as Uint8Array (PNG, JPEG, or WebP)
   * @param data - Text (UTF-8) or raw bytes to embed
   * @param format - Output format: 'png' (recommended), 'jpg', or 'webp'
   * @returns Watermarked image bytes
   */
  async embed(
    imageData: Uint8Array,
    data: string | Uint8Array,
    format: OutputFormat = 'png'
  ): Promise<Uint8Array> {
    const module = await init();
    this.applyConfig(module);

    const isText = typeof data === 'string';
    const payload = isText ? new TextEncoder().encode(data) : data;

    const result = module.embedSelfDescribing(imageData, payload, isText, format);
    if (result.error) {
      throw new Error(result.error);
    }
    return new Uint8Array(result.imageData);
  }

  /**
   * Extract a self-describing watermark without knowing its length.
   * @param imageData - Image data as Uint8Array
   * @returns The extraction result, or `null` if no watermark was found
   *          (wrong password or no watermark). Check `.valid` for integrity.
   */
  async extract(imageData: Uint8Array): Promise<ExtractResult | null> {
    const module = await init();
    this.applyConfig(module);

    const r = module.extractSelfDescribing(imageData);
    if (!r.found) return null;

    const bytes = new Uint8Array(r.payload);
    return {
      found: true,
      valid: !!r.valid,
      version: r.version | 0,
      isText: !!r.isText,
      bytes,
      text: r.isText ? new TextDecoder().decode(bytes) : undefined,
    };
  }

  /**
   * Check whether an image carries a watermark for this config, without
   * materialising the payload as text.
   * @param imageData - Image data as Uint8Array
   */
  async detect(imageData: Uint8Array): Promise<DetectResult> {
    const module = await init();
    this.applyConfig(module);

    const r = module.extractSelfDescribing(imageData);
    return {
      found: !!r.found,
      valid: !!r.valid,
      version: r.version | 0,
      isText: !!r.isText,
      byteLength: r.found && r.payload ? r.payload.length : 0,
    };
  }
}

// Export convenience functions
export async function embedStringWatermark(
  imageData: Uint8Array,
  watermarkText: string,
  config: WatermarkConfig = {},
  format: 'png' | 'jpg' | 'jpeg' | 'webp' = 'png'
): Promise<EmbedResult> {
  const bwm = new BlindWatermark(config);
  return bwm.embedString(imageData, watermarkText, format);
}

export async function extractStringWatermark(
  imageData: Uint8Array,
  wmBitLength: number,
  config: WatermarkConfig = {}
): Promise<string> {
  const bwm = new BlindWatermark(config);
  return bwm.extractString(imageData, wmBitLength);
}

/** Self-describing embed (recommended) — no length to track on extraction. */
export async function embedWatermark(
  imageData: Uint8Array,
  data: string | Uint8Array,
  config: WatermarkConfig = {},
  format: OutputFormat = 'png'
): Promise<Uint8Array> {
  const bwm = new BlindWatermark(config);
  return bwm.embed(imageData, data, format);
}

/** Self-describing extract (recommended) — returns null if no watermark is found. */
export async function extractWatermark(
  imageData: Uint8Array,
  config: WatermarkConfig = {}
): Promise<ExtractResult | null> {
  const bwm = new BlindWatermark(config);
  return bwm.extract(imageData);
}

export default BlindWatermark;
