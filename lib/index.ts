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

    // Set config
    module.setConfig(
      this.config.passwordWm,
      this.config.passwordImg,
      this.config.d1,
      this.config.d2,
      this.config.blockSize,
      this.config.dwtLevel,
      this.config.redundancy
    );

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

    // Set config
    module.setConfig(
      this.config.passwordWm,
      this.config.passwordImg,
      this.config.d1,
      this.config.d2,
      this.config.blockSize,
      this.config.dwtLevel,
      this.config.redundancy
    );

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

    // Set config
    module.setConfig(
      this.config.passwordWm,
      this.config.passwordImg,
      this.config.d1,
      this.config.d2,
      this.config.blockSize,
      this.config.dwtLevel,
      this.config.redundancy
    );

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

    // Set config
    module.setConfig(
      this.config.passwordWm,
      this.config.passwordImg,
      this.config.d1,
      this.config.d2,
      this.config.blockSize,
      this.config.dwtLevel,
      this.config.redundancy
    );

    // Call WASM function
    return module.extractBinaryWatermark(imageData, wmBitLength);
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

export default BlindWatermark;
