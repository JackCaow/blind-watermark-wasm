# Changelog

## 2.0.0

### Breaking

- **Deterministic block placement.** Bit placement now uses a fixed,
  toolchain-independent PRNG (SplitMix64 + Fisher-Yates) instead of `std::shuffle`,
  whose algorithm the C++ standard does not pin. This locks the watermark format so
  it can never be silently broken by a future Emscripten/libc++ change. The trade-off
  is a one-time format change: **watermarks embedded with 1.x are not readable by 2.0**
  — they report `found: false` (never corrupt data). Re-embed with 2.0.
- **Extraction now fails loudly on invalid input.** `extract()`, `detect()`,
  `extractString()`, and `extractBinary()` now **throw** when the input cannot be
  decoded as an image, instead of returning `null` / `""`. `null` from `extract()`
  is now reserved strictly for "decoded fine, but no watermark / wrong password".
  Wrap calls in try/catch when feeding untrusted files.
- **`embedBinary()` rejects malformed bit strings.** A `bits` argument containing
  anything other than `'0'`/`'1'` now throws, instead of silently coercing stray
  characters to `0` (which used to embed a wrong payload, e.g. `"10x1"` → `"1001"`).

### Added

- Image size guard: inputs above ~10 megapixels are rejected with a catchable error
  instead of aborting the wasm instance on a bad allocation.
- `script/run_bench.sh` robustness benchmark, and a measured robustness matrix +
  a Limitations section in the README.

### Internal

- Low-level WASM extract bindings now return `{ text } | { error }` /
  `{ bits } | { error }`, so a real failure is distinguishable from an empty result.

### Packaging

- Added a `prepack` hook so `npm pack` / `npm publish` / `npm run smoke` rebuild
  `dist` and fail with a clear error if the WASM glue is missing — never shipping a
  broken tarball.

### Docs / meta

- README: shields.io badges, live demo (GitHub Pages) and npm links.
- `package.json`: `homepage` (demo) and `bugs` for the npm registry page.

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
