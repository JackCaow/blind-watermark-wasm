// Copy the Emscripten glue + wasm next to the bundled dist entry.
//
// build:ts keeps `./blind_watermark.js` external (not bundled), so the published
// dist/index.{js,mjs} import it relative to dist/. The Emscripten runtime then
// locates blind_watermark.wasm next to that glue file — so both must live in dist/.
import { copyFileSync, existsSync, mkdirSync } from 'fs';

const files = ['blind_watermark.js', 'blind_watermark.wasm'];

mkdirSync('dist', { recursive: true });
for (const f of files) {
  const src = `lib/${f}`;
  if (!existsSync(src)) {
    console.error(`Missing ${src} — run "npm run build:wasm" first.`);
    process.exit(1);
  }
  copyFileSync(src, `dist/${f}`);
  console.log(`copied ${src} -> dist/${f}`);
}
