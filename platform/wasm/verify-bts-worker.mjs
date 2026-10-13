// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium,webkit}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const isWebKit=process.env.PROBE_BROWSER==='webkit';
const browser=await (isWebKit?webkit:chromium).launch(isWebKit?{headless:true}:{headless:true,executablePath:process.env.CHROME_PATH,args:['--enable-unsafe-webgpu']});
const page=await browser.newPage({viewport:{width:1000,height:1200}});
const out=path.resolve(process.env.PROBE_OUTPUT || 'out/Wasm/validation-bts-worker');await fs.mkdir(out,{recursive:true});
const errors=[],workers=[];page.on('pageerror',e=>errors.push(String(e)));page.on('worker',w=>workers.push(w.url()));
const read=()=>page.evaluate(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])));
const flat=n=>[n,...n.children.flatMap(flat)];
const texts=r=>flat(r.tree).filter(n=>n.tag==='text').map(n=>n.text);
const click=async(x,y)=>{const b=await page.locator('#canvas').boundingBox();await page.mouse.click(b.x+1+x,b.y+1+y);};
const load=async(waitReady=true)=>{const started=Date.now();await page.locator('#bundle-file').setInputFiles(path.resolve('out/Wasm/interactive-worker.lynx.bundle'));await page.locator('#load-bundle').click();await page.waitForFunction(()=>document.querySelector('#load-status').dataset.state==='success');const painted=Date.now()-started;if(waitReady)await page.waitForFunction(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).backgroundRuntime);return painted;};
const waitText=text=>page.waitForFunction(t=>{const flat=n=>[n,...n.children.flatMap(flat)];return flat(JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).tree).some(n=>n.text===t);},text,{timeout:20000});
const checks={};
try {
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6120/');
 await page.waitForFunction(()=>window.probeResult || window.probeErrors?.length,null,{timeout:60000});
 assert.deepEqual(await page.evaluate(()=>window.probeErrors),[]);
 await page.waitForFunction(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).backgroundRuntime);
 const first=await read();assert.notEqual(first.btsThread,first.mainThread);assert(first.btsThread>0);assert.equal(workers.length,1);
 assert(await page.evaluate(()=>crossOriginIsolated && window.lynxCanvas.HEAPU8.buffer instanceof SharedArrayBuffer));
 // The source image advances while a real BTS QuickJS script monopolizes its worker.
 await page.evaluate(()=>window.lynxCanvas.ccall('test_bts_busy',null,['number'],[1200]));
 await page.waitForFunction(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).btsWorkerBusy);
 const busyStart=await read();await page.waitForTimeout(650);const during=await read();
 assert(during.btsWorkerBusy);assert(during.frames>busyStart.frames+2);
 await page.waitForFunction(()=>!JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).btsWorkerBusy);
 assert.deepEqual((await read()).errors,[]);
 checks.isolation={passed:true,mainThread:first.mainThread,btsThread:first.btsThread,workerCount:workers.length,sharedMemory:true,framesWhileBTSBusy:[busyStart.frames,during.frames]};
 await load();await waitText('Count: 0');
 for(let i=1;i<=10;i++){await click(350,220);await waitText(`Count: ${i}`);}
 checks.events={passed:true,counter:10};
 // MTS click remains executable while BTS is busy; BTS events wait then resume.
 await page.evaluate(()=>window.lynxCanvas.ccall('test_bts_busy',null,['number'],[1200]));
 await page.waitForFunction(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).btsWorkerBusy);
 await click(100,280);
 await page.waitForFunction(()=>{const flat=n=>[n,...n.children.flatMap(flat)];return flat(JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).tree).some(n=>n.background===0xff23765b);});
 assert((await read()).btsWorkerBusy);
 await click(350,220);assert(texts(await read()).includes('Count: 10'));
 await waitText('Count: 11');assert.deepEqual((await read()).errors,[]);
 checks.affinity={passed:true,mtsClickDuringBusy:true,btsEventDeferred:true,btsEventResumed:true};
 await click(620,215);await waitText('Stop animation');const running=await read();await page.waitForTimeout(1500);const animated=await read();
 assert(animated.frames>running.frames+40);
 await click(620,215);await waitText('Start animation');await page.waitForTimeout(200);const idle=(await read()).frames;await page.waitForTimeout(400);assert.equal((await read()).frames,idle);
 checks.raf={passed:true,frames:[running.frames,animated.frames],stopped:true};
 // Reload while QJS is busy: no main-thread join, old callbacks cannot touch new nodes.
 await page.evaluate(()=>window.lynxCanvas.ccall('test_bts_busy',null,['number'],[1200]));
 await page.waitForFunction(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).btsWorkerBusy);
 const start=Date.now();const firstPaint=await load(false);assert(firstPaint<600);assert.equal((await read()).backgroundRuntime,false);await page.waitForFunction(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).backgroundRuntime);await waitText('Count: 0');const reloadTime=Date.now()-start;
 for(let i=0;i<5;i++){await load();await waitText('Count: 0');await click(350,220);await waitText('Count: 1');}
 const final=await read();assert.equal(final.btsThread,first.btsThread);assert.equal(workers.length,1);assert.deepEqual(final.errors,[]);
 checks.reload={passed:true,firstPaintWhileOldWorkerBusyMilliseconds:firstPaint,backgroundReadyMilliseconds:reloadTime,repeatedReloads:5,persistentWorker:true};
 assert.deepEqual(errors,[]);assert.deepEqual(await page.evaluate(()=>window.probeErrors),[]);
 await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:true,browser:await browser.version(),checks,report:final},null,2));await page.screenshot({path:path.join(out,'browser.png')});console.log(JSON.stringify({passed:true,checks},null,2));
} catch(error){await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:false,error:String(error),stack:error.stack,checks,report:await read().catch(()=>null)},null,2));throw error;}finally{await browser.close();}
