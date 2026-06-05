# Changelog

## 2.0.0

### Breaking

- **Deterministic block placement.** Bit placement now uses a fixed,
  toolchain-independent PRNG (SplitMix64 + Fisher-Yates) instead of `std::shuffle`,
  whose algorithm the C++ standard does not pin. This locks the watermark format so
  it can never be silently broken by a future Emscripten/libc++ change. The trade-off
  is a one-time format change: **watermarks embedded with 1.x are not readable by 2.0**
  — they report `found: false` (never corrupt data). Re-embed with 2.0.

### Added

- Image size guard: inputs above ~10 megapixels are rejected with a catchable error
  instead of aborting the wasm instance on a bad allocation.
- `script/run_bench.sh` robustness benchmark, and a measured robustness matrix +
  a Limitations section in the README.

## 1.1.0

### Added

- Self-describing watermark API: `embed` / `extract` / `detect` recover the payload
  with no `wmBitLength` to track. An 8-byte header (magic + version + length + CRC-16)
  is embedded ahead of the payload. The legacy `embedString` / `extractString(img, len)`
  API is unchanged.

## 1.0.0

- Initial release: DWT-DCT-SVD blind watermarking compiled to WebAssembly, for
  browsers, Node.js, and Electron. PNG/JPEG/WebP, alpha preserved, redundancy for
  robustness, full TypeScript types.
