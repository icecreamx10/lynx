// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_PATH || 'playwright');
const out = path.resolve(process.env.PROBE_OUTPUT || 'out/Wasm/validation');
await fs.mkdir(out, { recursive: true });
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || undefined,
  headless: true,
  args: ['--enable-unsafe-webgpu', ...(process.env.PROBE_ALLOW_HTTP_ORIGIN
    ? ['--unsafely-treat-insecure-origin-as-secure=' + process.env.PROBE_ALLOW_HTTP_ORIGIN] : [])],
});
const page = await browser.newPage({ viewport: { width: 1000, height: 850 },
  ignoreHTTPSErrors: process.env.PROBE_LOCAL_CERT === '1' });
const errors = [];
const messages = [];
const requests = [];
page.on('request', request => requests.push(request.url()));
page.on('pageerror', error => errors.push(error.stack || String(error)));
page.on('console', message => messages.push({ type: message.type(), text: message.text() }));
try {
  if (process.env.PROBE_DOCUMENT_ONLY === '1') {
    await page.route('**/*', route => route.request().resourceType() === 'document'
      ? route.continue() : route.abort());
  }
  await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6091/', { waitUntil: 'networkidle' });
  await page.waitForFunction(() => window.probeResult || window.probeErrors?.length, { timeout: 60000 });
  const first = await page.evaluate(() => ({ result: window.probeResult, errors: window.probeErrors }));
  assert.deepEqual(first.errors, []);
  assert.deepEqual(errors, []);
  if (process.env.PROBE_DOCUMENT_ONLY === '1') assert.equal(requests.length, 1);
  assert.equal(first.result.qjs, true);
  assert.equal(first.result.layout, 'lynx-starlight');
  assert.equal(first.result.renderer, 'skity-webgpu');
  assert.equal(first.result.nodes, 8);
  assert.equal(first.result.width, 800);
  assert.equal(first.result.height, 500);
  // Sample immediately after a new frame; WebGPU canvas contents need not
  // survive the browser's next compositing step.
  const pixels = await page.evaluate(() => {
    const code = window.lynxCanvas.ccall('render_scene', 'number', [], []);
    const copy = document.createElement('canvas');
    copy.width = 800; copy.height = 500;
    const context = copy.getContext('2d');
    context.drawImage(document.querySelector('#canvas'), 0, 0);
    const sample = (x, y) => [...context.getImageData(x, y, 1, 1).data];
    const colors = [[10, 10], [50, 80], [50, 300], [300, 300], [550, 300]].map(([x,y]) => sample(x,y));
    const text = context.getImageData(48, 48, 450, 34).data;
    let lightPixels = 0;
    for (let i = 0; i < text.length; i += 4) if (text[i] > 150 && text[i+1] > 150 && text[i+2] > 150) ++lightPixels;
    return { code, colors, lightPixels, png: copy.toDataURL('image/png') };
  });
  assert.equal(pixels.code, 0);
  assert.deepEqual(pixels.colors, [
    [16, 24, 39, 255], [25, 38, 61, 255],
    [23, 74, 72, 255], [40, 61, 103, 255], [80, 54, 99, 255],
  ]);
  assert(pixels.lightPixels > 200, `Expected visible text, found ${pixels.lightPixels} light pixels`);
  await fs.writeFile(path.join(out, 'canvas.png'), Buffer.from(pixels.png.split(',')[1], 'base64'));
  const repeats = await page.evaluate(() => {
    for (let i = 0; i < 10; i++) if (window.lynxCanvas.ccall('render_scene', 'number', [], []) !== 0) return null;
    return JSON.parse(window.lynxCanvas.ccall('scene_report', 'string', [], []));
  });
  assert.equal(repeats.frames, 12);
  await page.waitForTimeout(250);
  const gpuErrors = await page.evaluate(() => window.probeErrors);
  assert.deepEqual(gpuErrors, []);
  const cleanup = await page.evaluate(async () => {
    await window.probeDevice.queue.onSubmittedWorkDone();
    window.lynxCanvas.ccall('release_completed_frames', null, [], []);
    const code = window.lynxCanvas.ccall('render_scene', 'number', [], []);
    await window.probeDevice.queue.onSubmittedWorkDone();
    window.lynxCanvas.ccall('release_completed_frames', null, [], []);
    return { code, result: JSON.parse(window.lynxCanvas.ccall('scene_report', 'string', [], [])),
      errors: window.probeErrors, adapter: window.probeAdapter, secureContext: window.isSecureContext };
  });
  assert.equal(cleanup.code, 0);
  assert.equal(cleanup.result.frames, 13);
  assert.deepEqual(cleanup.errors, []);
  assert.deepEqual(errors, []);
  await page.screenshot({ path: path.join(out, 'browser.png') });
  const report = { passed: true, browser: browser.version(), first: first.result, repeated: repeats,
    colors: pixels.colors, textLightPixels: pixels.lightPixels, cleanup, requests, errors, gpuErrors, messages };
  await fs.writeFile(path.join(out, 'validation.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
} catch (error) {
  const status = await page.locator('#status').textContent().catch(() => null);
  await fs.writeFile(path.join(out, 'validation.json'), JSON.stringify({ passed: false, error: String(error), status, errors, messages }, null, 2));
  throw error;
} finally {
  await browser.close();
}
