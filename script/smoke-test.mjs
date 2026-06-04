// Round-trips a watermark using the INSTALLED package (resolved via its package
// entry -> external glue -> colocated .wasm), proving the published tarball works
// end to end. Run from a directory that has test.png and the package installed.
import fs from 'fs';
import { BlindWatermark } from 'blind-watermark-wasm';

const bwm = new BlindWatermark();
const img = new Uint8Array(fs.readFileSync('test.png'));

const text = 'smoke';
const { imageData, wmBitLength } = await bwm.embedString(img, text, 'png');
const got = await bwm.extractString(imageData, wmBitLength);

if (got !== text) {
  console.error(`FAIL: extracted "${got}", expected "${text}"`);
  process.exit(1);
}
console.log('Smoke test PASSED: installed package round-trips correctly');
