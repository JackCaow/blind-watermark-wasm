// Copy the Emscripten glue next to the bundled dist entry.
//
// build:ts keeps `./blind_watermark.js` external (not bundled), so the published
// dist/index.{js,mjs} import it relative to dist/. With SINGLE_FILE=1 the wasm is
// base64-inlined into the glue .js, so there is normally no separate .wasm to ship
// (it is copied only if a non-inlined build produced one).
import { copyFileSync, existsSync, mkdirSync } from 'fs';

mkdirSync('dist', { recursive: true });

// Required: the glue.
const glue = 'lib/blind_watermark.js';
if (!existsSync(glue)) {
  console.error(`Missing ${glue} — run "npm run build:wasm" first.`);
  process.exit(1);
}
copyFileSync(glue, 'dist/blind_watermark.js');
console.log('copied lib/blind_watermark.js -> dist/blind_watermark.js');

// Optional: a separate .wasm only exists for non-single-file builds.
if (existsSync('lib/blind_watermark.wasm')) {
  copyFileSync('lib/blind_watermark.wasm', 'dist/blind_watermark.wasm');
  console.log('copied lib/blind_watermark.wasm -> dist/blind_watermark.wasm');
}
