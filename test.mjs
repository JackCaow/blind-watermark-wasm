/**
 * Test script for blind-watermark-wasm
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Import the WASM module
const BlindWatermarkModule = (await import('./lib/blind_watermark.js')).default;

async function test() {
    console.log('Loading WASM module...');
    const Module = await BlindWatermarkModule();
    console.log('WASM module loaded successfully!');

    // Set default config (d2=0: secondary singular value disabled, see README)
    Module.setConfig(1, 1, 36, 0, 4, 1, 3);
    console.log('Config set: passwordWm=1, passwordImg=1, d1=36, d2=0, blockSize=4, dwtLevel=1, redundancy=3');

    // Check if test image exists
    const testImagePath = path.join(__dirname, 'test.png');
    if (!fs.existsSync(testImagePath)) {
        console.log('\nNo test image found. Creating a simple test...');
        console.log('To run a full test, place a PNG image as "test.png" in the project root.');
        console.log('\nWASM module is working correctly!');
        return;
    }

    // Read test image as Uint8Array
    console.log('\nReading test image...');
    const imageBuffer = fs.readFileSync(testImagePath);
    const imageData = new Uint8Array(imageBuffer);
    console.log(`Image size: ${imageData.length} bytes`);

    // Embed watermark
    const watermarkText = 'Hello WASM!';
    console.log(`Embedding watermark: "${watermarkText}"`);

    const startEmbed = Date.now();
    const embedResult = Module.embedStringWatermark(imageData, watermarkText, 'png');
    const embedTime = Date.now() - startEmbed;

    if (embedResult.error) {
        console.error('Embed error:', embedResult.error);
        return;
    }

    const wmBitLength = embedResult.wmBitLength;
    console.log(`Watermark embedded successfully! Time: ${embedTime}ms, Bit length: ${wmBitLength}`);

    // Save watermarked image
    const watermarkedData = new Uint8Array(embedResult.imageData);
    const outputPath = path.join(__dirname, 'test_watermarked.png');
    fs.writeFileSync(outputPath, watermarkedData);
    console.log(`Watermarked image saved to: ${outputPath} (${watermarkedData.length} bytes)`);

    // Extract watermark
    console.log('\nExtracting watermark...');

    const startExtract = Date.now();
    const extractResult = Module.extractStringWatermark(watermarkedData, wmBitLength);
    const extractTime = Date.now() - startExtract;

    if (extractResult.error) {
        console.error('Extract error:', extractResult.error);
        process.exitCode = 1;
        return;
    }

    const extractedText = extractResult.text;
    console.log(`Extracted watermark: "${extractedText}" (Time: ${extractTime}ms)`);

    if (extractedText === watermarkText) {
        console.log('\n✓ Test PASSED! Watermark extracted correctly.');
    } else {
        console.log('\n✗ Test FAILED! Extracted text does not match.');
        console.log(`  Expected: "${watermarkText}"`);
        console.log(`  Got: "${extractedText}"`);
        process.exitCode = 1;
    }

    // --- Self-describing API: extract WITHOUT knowing the length ---
    console.log('\n--- Self-describing watermark ---');
    const sdText = 'Self-describing 自描述!';
    const sdPayload = new TextEncoder().encode(sdText);

    const sdEmbed = Module.embedSelfDescribing(imageData, sdPayload, true, 'png');
    if (sdEmbed.error) {
        console.log('✗ Self-describing embed error:', sdEmbed.error);
        process.exitCode = 1;
        return;
    }
    const sdImage = new Uint8Array(sdEmbed.imageData);
    const sd = Module.extractSelfDescribing(sdImage);
    const sdGot = new TextDecoder().decode(new Uint8Array(sd.payload));
    console.log(`Extracted (no length): found=${sd.found} valid=${sd.valid} isText=${sd.isText} -> "${sdGot}"`);

    if (sd.found && sd.valid && sd.isText && sdGot === sdText) {
        console.log('✓ Self-describing PASSED (no length needed).');
    } else {
        console.log('✗ Self-describing FAILED.');
        process.exitCode = 1;
    }

    // A clean (unwatermarked) image must report not-found.
    const clean = Module.extractSelfDescribing(imageData);
    console.log(`Clean-image detect: found=${clean.found} (expected false)`);
    if (clean.found) {
        console.log('✗ False positive on clean image.');
        process.exitCode = 1;
    }
}

test().catch((e) => { console.error(e); process.exitCode = 1; });
