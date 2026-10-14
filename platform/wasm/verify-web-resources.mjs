// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium,webkit}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const wk=process.env.PROBE_BROWSER==='webkit';
const browser=await (wk?webkit:chromium).launch(wk?{headless:true}:{headless:true,executablePath:process.env.CHROME_PATH,args:['--enable-unsafe-webgpu']});
const base=process.env.PROBE_URL || 'http://127.0.0.1:6121/';
const out=path.resolve(process.env.PROBE_OUTPUT || 'out/Wasm/validation-web-resources');
await fs.mkdir(out,{recursive:true});
const checks={}, errors=[];
const flat=n=>[n,...n.children.flatMap(flat)];
const read=p=>p.evaluate(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])));
const fonts=p=>p.evaluate(()=>window.lynxCanvas.lynxFonts.inspect());
const ready=p=>p.waitForFunction(()=>window.probeResult||window.probeErrors?.length,null,{timeout:90000});
const cjkReady=p=>p.waitForFunction(()=>window.lynxCanvas.lynxFonts.inspect()[1].state==='loaded',null,{timeout:90000});
const upload=async(p,file)=>{
 await p.locator('#bundle-file').setInputFiles(path.resolve(file));
 await p.locator('#load-bundle').click();
 await p.waitForFunction(()=>document.querySelector('#load-status').dataset.state==='success');
};
const newPage=async()=>{const p=await browser.newPage();p.on('pageerror',e=>errors.push(String(e)));return p;};
try {
 // Only the startup font is requested for an ASCII page. A later real Lynx
 // bundle introduces CJK, without recreating the WASM instance or worker.
 let p=await newPage();await p.goto(new URL('interactive/',base).href);await ready(p);
 assert.deepEqual((await fonts(p)).map(f=>f.state),['loaded','idle','idle','idle']);
 assert.deepEqual(await p.evaluate(()=>window.lynxCanvas.lynxResources.inspect().filter(r=>r.type==='font').map(r=>r.url.split('/').pop())),['Roboto-Regular.ttf']);
 const thread=(await read(p)).btsThread;
 await upload(p,'out/Wasm/text.lynx.bundle');await cjkReady(p);
 await p.waitForFunction(()=>window.lynxCanvas.lynxFonts.inspect().every(f=>f.state==='loaded'));
 assert.equal((await read(p)).btsThread,thread);
 assert(flat((await read(p)).tree).filter(n=>n.textLayout).every(n=>n.textLayout.missingGlyphs===0));
 checks.onDemand={passed:true,ASCIIRequestsOnlyLatin:true,laterBundleLoadsCJK:true,persistentWorker:true};await p.close();
 // Hold a real font response until after the page has painted, then check
 // native layout and following siblings change when the font arrives.
 p=await newPage();let release;const gate=new Promise(resolve=>release=resolve);
 await p.route('**/fonts/NotoSansSC.ttf',async route=>{await gate;await route.fulfill({body:await fs.readFile('out/NotoSansSC.ttf'),contentType:'font/ttf'}).catch(()=>{});});
 await p.goto(base,{waitUntil:'domcontentloaded'});await ready(p);
 await p.waitForFunction(()=>window.lynxCanvas.lynxFonts.inspect()[1].state==='loading');
 const before=await read(p),oldWrap=flat(before.tree).find(n=>n.id==='wrap');
 assert(oldWrap.textLayout.missingGlyphs>0);assert(before.frames>0);
 release();await cjkReady(p);await p.waitForTimeout(300);
 const after=await read(p),wrap=flat(after.tree).find(n=>n.id==='wrap');
 assert.equal(wrap.textLayout.missingGlyphs,0);assert(wrap.textLayout.lines.length>=3);
 assert(after.frames>before.frames);assert.notEqual(wrap.height,oldWrap.height);
 assert.equal(wrap.height,wrap.textLayout.height);
 const last=(await read(p)).frames;await p.waitForTimeout(400);assert.equal((await read(p)).frames,last,'Font arrival causes idle redraw loop');
 checks.delayedFont={passed:true,paintBeforeFont:true,automaticLayoutAndPaint:true,beforeHeight:oldWrap.height,afterHeight:wrap.height,idleStable:true};
 await p.screenshot({path:path.join(out,'delayed-font.png')});await p.close();
 // Missing/corrupt font data is observable and retryable without an engine
 // abort. Failure must not be inserted as an empty face.
 for(const mode of ['404','corrupt']) {
  p=await newPage();let fail=true;
  await p.route('**/fonts/NotoSansSC.ttf',async route=>{
   if(fail)await route.fulfill(mode==='404'?{status:404,body:'missing'}:{body:Buffer.from('not a font'),contentType:'font/ttf'});
   else await route.fulfill({body:await fs.readFile('out/NotoSansSC.ttf'),contentType:'font/ttf'});
  });
  await p.goto(base);await ready(p);
  await p.waitForFunction(()=>window.lynxCanvas.lynxFonts.inspect()[1].state==='error');
  assert.deepEqual((await read(p)).errors,[]);assert((await fonts(p))[1].error);
  fail=false;await p.evaluate(()=>window.lynxCanvas.lynxFonts.retry(1));await cjkReady(p);
  assert.equal(flat((await read(p)).tree).find(n=>n.id==='wrap').textLayout.missingGlyphs,0);
  checks[mode==='404'?'HTTPFailureRetry':'invalidFontRetry']={passed:true};await p.close();
 }
 // A page replacement aborts its pending font request. The late response may
 // not register that face or reflow the replacement; a later page retries.
 p=await newPage();let releaseOld;const oldGate=new Promise(resolve=>releaseOld=resolve);let first=true;
 await p.route('**/fonts/NotoSansSC.ttf',async route=>{if(first){first=false;await oldGate;}await route.fulfill({body:await fs.readFile('out/NotoSansSC.ttf'),contentType:'font/ttf'}).catch(()=>{});});
 await p.goto(base,{waitUntil:'domcontentloaded'});await ready(p);
 await p.waitForFunction(()=>window.lynxCanvas.lynxFonts.inspect()[1].state==='loading');
 await upload(p,'out/Wasm/interactive-worker.lynx.bundle');releaseOld();await p.waitForTimeout(300);
 assert.equal((await fonts(p))[1].state,'idle');
 assert(await p.evaluate(()=>window.lynxCanvas.lynxResources.inspect().some(r=>r.type==='font'&&r.url.endsWith('/NotoSansSC.ttf')&&r.state==='cancelled')));
 assert(flat((await read(p)).tree).some(n=>n.text==='Count: 0'));
 await upload(p,'out/Wasm/text.lynx.bundle');await cjkReady(p);
 assert.equal(flat((await read(p)).tree).find(n=>n.id==='wrap').textLayout.missingGlyphs,0);
 checks.cancelOnReload={passed:true,abort:true,lateResultIgnored:true,nextPageRetries:true};await p.close();
 // Both consumers use the same injected provider, including custom schemes.
 p=await newPage();await p.addInitScript(()=>{
  window.resourceCalls=[];
  window.lynxFontResources={0:'devtools://fonts/Roboto-Regular.ttf',1:'devtools://fonts/NotoSansSC.ttf',2:'devtools://fonts/NotoSansArabic.ttf',3:'devtools://fonts/NotoEmoji.ttf'};
  window.lynxResourceProvider=async request=>{
   window.resourceCalls.push({type:request.type,url:request.url});
   const url=new URL(request.url);
   if(url.protocol==='devtools:')return fetch(new URL('fonts'+url.pathname,document.baseURI),{signal:request.signal});
   return fetch(url,{signal:request.signal});
  };
 });
 await p.goto(base);await ready(p);await cjkReady(p);
 await upload(p,'out/Wasm/images-resource.lynx.bundle');
 await p.waitForFunction(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).images.filter(r=>r.loaded).length===13);
 const calls=await p.evaluate(()=>window.resourceCalls);
 assert(calls.some(r=>r.type==='font'&&r.url.startsWith('devtools:')));
 assert(calls.some(r=>r.type==='image'));
 assert(await p.evaluate(()=>window.lynxCanvas.lynxResources.inspect().some(r=>r.type==='font'&&r.state==='loaded'&&r.source.startsWith('devtools:'))));
 checks.customProvider={passed:true,fontAndImage:true,customScheme:true,diagnostics:true};
 assert.deepEqual(await p.evaluate(()=>window.probeErrors),[]);await p.close();
 assert.deepEqual(errors,[]);
 await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:true,browser:await browser.version(),checks},null,2));
 console.log(JSON.stringify({passed:true,checks},null,2));
} catch(error) {
 await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:false,error:String(error),stack:error.stack,checks,errors},null,2));throw error;
} finally {await browser.close();}
