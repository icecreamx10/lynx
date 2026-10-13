// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_PATH || 'playwright');
const bundleMode = process.env.PROBE_BUNDLE === '1';
const out = path.resolve(process.env.PROBE_OUTPUT || 'out/Wasm/validation');
await fs.mkdir(out, { recursive: true });
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || undefined,
  headless: true,
  args: ['--enable-unsafe-webgpu', ...(process.env.PROBE_ALLOW_HTTP_ORIGIN
    ? ['--unsafely-treat-insecure-origin-as-secure=' + process.env.PROBE_ALLOW_HTTP_ORIGIN] : [])],
});
const page = await browser.newPage({ viewport: { width: 1000, height: 1100 },
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
  assert.equal(first.result.nodes, bundleMode ? 13 : 8);
  if (bundleMode) {
    assert.equal(first.result.fullLynxBundle, true);
    assert.equal(first.result.bundleLoaded, true);
    assert.equal(first.result.backgroundRuntime, false);
    assert(first.result.bundleBytes > 80000);
    assert.deepEqual(first.result.errors, []);
    const flat = node => [node, ...node.children.flatMap(flat)];
    const textNodes = flat(first.result.tree).filter(node => node.tag === 'text');
    assert.deepEqual(textNodes.map(node => node.text), [
      'A real Lynx page', 'ReactLynx bundle running in WASM',
      'Bundle decoded', 'QJS executed', 'WebGPU canvas',
      'JSX + CSS -> Lynx bundle -> native renderer',
    ]);
    assert.deepEqual([textNodes[0].x, textNodes[0].y, textNodes[0].fontSize], [48,40,26]);
    assert.deepEqual(textNodes.slice(2,5).map(node => [node.x,node.y]), [[48,192],[293,192],[539,192]]);
  }
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
  let invalidBundle = null;
  if (bundleMode) {
    invalidBundle = await page.evaluate(async () => {
      const code = window.lynxCanvas.ccall('verify_corrupt_bundle', 'number', [], []);
      const redraw = window.lynxCanvas.ccall('render_scene', 'number', [], []);
      await window.probeDevice.queue.onSubmittedWorkDone();
      window.lynxCanvas.ccall('release_completed_frames', null, [], []);
      return { code, redraw, result: JSON.parse(window.lynxCanvas.ccall('scene_report', 'string', [], [])), errors: window.probeErrors };
    });
    assert.equal(invalidBundle.code, 0);
    assert.equal(invalidBundle.redraw, 0);
    assert.equal(invalidBundle.result.frames, 14);
    assert.deepEqual(invalidBundle.result.errors, []);
    assert.deepEqual(invalidBundle.errors, []);
    assert.deepEqual(errors, []);
  }
  let uploads = null;
  if (bundleMode) {
    const fileInput = page.locator('#bundle-file');
    const loadButton = page.locator('#load-bundle');
    assert.match(await page.locator('.loader').textContent(), /LynxJS.*编译生成/s);
    await fileInput.setInputFiles(path.resolve('platform/wasm/app/dist/main.lynx.bundle'));
    await loadButton.click();
    await page.waitForFunction(() => document.querySelector('#load-status').dataset.state === 'success');
    const uploaded = await page.evaluate(() => JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])));
    assert.equal(uploaded.bundleSequence, 2);
    assert.equal(uploaded.nodes, 13);
    assert.equal(uploaded.frames, 1);
    assert.deepEqual(uploaded.tree, first.result.tree);
    await fileInput.setInputFiles({name:'broken.lynx.bundle',mimeType:'application/octet-stream',buffer:Buffer.from('invalid!')});
    await loadButton.click();
    await page.waitForFunction(() => document.querySelector('#load-status').dataset.state === 'error');
    const afterInvalid = await page.evaluate(() => JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])));
    assert.equal(afterInvalid.bundleSequence, 2);
    assert.deepEqual(afterInvalid.tree, uploaded.tree);
    const invalidMessage = await page.locator('#load-status').textContent();
    await fileInput.setInputFiles({name:'source.tsx',mimeType:'text/plain',buffer:Buffer.from('root.render(<view />)')});
    await loadButton.click();
    await page.waitForFunction(() => document.querySelector('#load-status').textContent.includes('不能加载源码'));
    const sourceMessage = await page.locator('#load-status').textContent();
    // Successful load again proves UI and engine recover after failed input.
    await fileInput.setInputFiles(path.resolve('platform/wasm/app/dist/main.lynx.bundle'));
    await loadButton.click();
    await page.waitForFunction(() => document.querySelector('#load-status').dataset.state === 'success');
    const recovered = await page.evaluate(() => JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])));
    assert.equal(recovered.bundleSequence, 3);
    assert.deepEqual(recovered.tree, first.result.tree);
    assert.deepEqual(recovered.errors, []);
    assert.deepEqual(await page.evaluate(() => window.probeErrors), []);
    assert.deepEqual(errors, []);
    if (process.env.PROBE_DOCUMENT_ONLY === '1') assert.equal(requests.length, 1);
    uploads = {uploaded, invalidMessage, sourceMessage, recovered};
  }
  await page.screenshot({ path: path.join(out, 'browser.png') });
  const report = { passed: true, browser: browser.version(), first: first.result, repeated: repeats,
    invalidBundle, uploads, colors: pixels.colors, textLightPixels: pixels.lightPixels, cleanup, requests, errors, gpuErrors, messages };
  await fs.writeFile(path.join(out, 'validation.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
} catch (error) {
  const status = await page.locator('#status').textContent().catch(() => null);
  await fs.writeFile(path.join(out, 'validation.json'), JSON.stringify({ passed: false, error: String(error), status, errors, messages }, null, 2));
  throw error;
} finally {
  await browser.close();
}
