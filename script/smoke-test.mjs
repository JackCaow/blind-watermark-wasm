// Round-trips a watermark using the INSTALLED package (resolved via its package
// entry -> external glue -> colocated .wasm), proving the published tarball works
// end to end. Run from a directory that has test.png and the package installed.
import fs from 'fs';
import { BlindWatermark } from 'blind-watermark-wasm';

// Higher redundancy than the default: this gate verifies the package plumbing
// works, not the robustness envelope (that's script/run_bench.sh). test.png is a
// real photo with saturated regions where the default redundancy can leave a
// single marginal bit, which would make this a flaky gate.
const bwm = new BlindWatermark({ redundancy: 6 });
const img = new Uint8Array(fs.readFileSync('test.png'));

// Low-level explicit-length API
const text = 'smoke';
const { imageData, wmBitLength } = await bwm.embedString(img, text, 'png');
const got = await bwm.extractString(imageData, wmBitLength);
if (got !== text) {
  console.error(`FAIL (embedString): extracted "${got}", expected "${text}"`);
  process.exit(1);
}

// Self-describing API (no length needed)
const sdText = 'smoke 自描述';
const wm = await bwm.embed(img, sdText, 'png');
const res = await bwm.extract(wm);
if (!res || !res.valid || res.text !== sdText) {
  console.error('FAIL (embed/extract):', res);
  process.exit(1);
}

console.log('Smoke test PASSED: installed package round-trips correctly (both APIs)');
