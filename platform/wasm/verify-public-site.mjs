// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
// Anonymous-browser gate, including hosts that ignore static _headers.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',args:['--enable-unsafe-webgpu']});
const context=await browser.newContext({viewport:{width:1500,height:1300}}),errors=[],checks={};
const base=process.env.PROBE_URL||'http://127.0.0.1:6173/',out=process.env.PROBE_OUT||'out/Wasm/validation-public-site';
await fs.mkdir(out,{recursive:true});
const flat=n=>[n,...n.children.flatMap(flat)];
async function open(path) {
 const p=await context.newPage();p.on('pageerror',e=>errors.push(String(e)));
 await p.goto(new URL(path,base).href,{waitUntil:'commit',timeout:90000});
 await p.waitForFunction(()=>window.probeResult?.backgroundRuntime||window.probeErrors?.length,null,{timeout:90000});
 assert.deepEqual(await p.evaluate(()=>probeErrors),[]);
 assert(await p.evaluate(()=>crossOriginIsolated&&typeof SharedArrayBuffer==='function'));
 const policy=await p.evaluate(async()=>{const r=await fetch(location.href);return {coop:r.headers.get('Cross-Origin-Opener-Policy'),coep:r.headers.get('Cross-Origin-Embedder-Policy')};});
 assert.deepEqual(policy,{coop:'same-origin',coep:'require-corp'});
 return p;
}
try {
 let p=await open('');assert((await p.evaluate(()=>lynxCanvas.call('state'))).report.renderWorker);
 await p.getByRole('button',{name:'关闭 Lynx DevTool',exact:true}).waitFor();
 const frame=await p.locator('#devtool-panel iframe').elementHandle().then(h=>h.contentFrame());await frame.locator('.elements-tree-outline').waitFor();
 const left=await p.locator('.preview-pane').boundingBox(),right=await p.locator('#devtool-controls').boundingBox();assert(right.x>=left.x+left.width);
 checks.root={anonymous:true,isolated:true,worker:true,officialFrontend:true,defaultOpen:true,sideBySide:true};
 await p.screenshot({path:out+'/root.png',fullPage:true});await p.close();
 p=await open('text/');await p.waitForFunction(()=>lynxCanvas.lynxFonts.inspect().every(f=>f.state==='loaded'),null,{timeout:90000});
 const text=await p.evaluate(()=>JSON.parse(lynxCanvas.ccall('scene_report','string',[],[])));assert(flat(text.tree).filter(n=>n.textLayout).every(n=>n.textLayout.missingGlyphs===0));checks.text={externalFonts:true,missingGlyphs:0};await p.close();
 p=await open('images/');await p.waitForFunction(()=>JSON.parse(lynxCanvas.ccall('scene_report','string',[],[])).images.filter(i=>i.loaded).length===13,null,{timeout:90000});checks.images={loaded:13};await p.close();
 p=await open('interactive/');const b=await p.locator('#canvas').boundingBox();await p.mouse.click(b.x+351,b.y+221);
 await p.waitForFunction(()=>{const f=n=>[n,...n.children.flatMap(f)];return f(JSON.parse(lynxCanvas.ccall('scene_report','string',[],[])).tree).some(n=>n.text==='Count: 1');});checks.interactive={reactCounter:true};await p.close();
 assert.deepEqual(errors,[]);await fs.writeFile(out+'/validation.json',JSON.stringify({passed:true,url:base,checks},null,2));console.log(JSON.stringify({passed:true,checks}));
} finally {await browser.close();}
