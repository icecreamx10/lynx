// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import http from 'node:http';
const require=createRequire(import.meta.url);
const {chromium,webkit}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const out=path.resolve(process.env.PROBE_OUTPUT || 'out/Wasm/validation-images');
const imageBundle=path.resolve(process.env.PROBE_IMAGE_BUNDLE || 'platform/wasm/app/dist/main.lynx.bundle');
await fs.mkdir(out,{recursive:true});
const isWebKit=process.env.PROBE_BROWSER==='webkit';
const browser=await (isWebKit?webkit:chromium).launch(isWebKit?{headless:true}:{executablePath:process.env.CHROME_PATH,headless:true,args:['--enable-unsafe-webgpu']});
const page=await browser.newPage({viewport:{width:1000,height:1200}});
if(process.env.PROBE_NO_IMAGE_DECODER==='1')await page.addInitScript(()=>{Object.defineProperty(globalThis,'ImageDecoder',{value:undefined,configurable:true});});
const errors=[],messages=[],checkpoints={};
const fixtureSource=await fs.readFile('platform/wasm/app/src/image-fixtures.ts','utf8');
const fixtures=JSON.parse(fixtureSource.slice(fixtureSource.indexOf('=')+1).trim().replace(/;$/,''));
const bytes=name=>Buffer.from(fixtures[name].split(',')[1],'base64');
const server=http.createServer((req,res)=>{
 if(req.url==='/allowed.png')res.setHeader('Access-Control-Allow-Origin','*');
 res.setHeader('Content-Type','image/png');res.end(bytes('png'));
});
await new Promise(resolve=>server.listen(6196,'127.0.0.1',resolve));
page.on('pageerror',e=>errors.push(String(e)));
page.on('console',m=>{if(messages.length<500)messages.push({type:m.type(),text:m.text()});});
const read=()=>page.evaluate(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])));
const inspect=()=>page.evaluate(()=>window.lynxCanvas.lynxImages.inspect());
const flat=n=>[n,...n.children.flatMap(flat)];
const texts=r=>flat(r.tree).filter(n=>n.tag==='text').map(n=>n.text);
const click=async(x,y)=>{const b=await page.locator('#canvas').boundingBox();await page.mouse.click(b.x+1+x*800/800,b.y+1+y);};
const wait=fn=>page.waitForFunction(fn,null,{timeout:20000});
const samplePoints=[[525,280],[615,280],[570,240],[570,280],[570,320],[650,235],[740,325],[430,265],[55,265]];
for(let f=0;f<3;f++){
 const x=20+f*250;samplePoints.push([x+50,78],[x+50,157],[x+10,118],[x+50,118],[x+90,118],[x+130,73],[x+220,163]);
}
const snapshot=()=>page.evaluate(async samples=>{
 window.pauseLynxFramePump=true;
 const m=window.lynxCanvas;
 await window.probeDevice.queue.onSubmittedWorkDone();m.ccall('release_completed_frames',null,[],[]);
 const id=m.ccall('render_frame','number',[],[]);
 const c=document.createElement('canvas');c.width=800;c.height=500;
 const ctx=c.getContext('2d');ctx.drawImage(document.querySelector('#canvas'),0,0);
 const data=ctx.getImageData(0,0,800,500).data;
 const pixels=Object.fromEntries(samples.map(([x,y])=>[`${x},${y}`,[...data.slice((y*800+x)*4,(y*800+x)*4+4)]]));
 const png=c.toDataURL();
 await window.probeDevice.queue.onSubmittedWorkDone();m.ccall('release_frames',null,['number'],[id]);
 window.pauseLynxFramePump=false;
 return {pixels,png};
},samplePoints);
// Capture browser-composited pixels without calling render_scene/render_frame.
// A forced validation draw would mask a broken invalidation/scheduling chain.
const compositorPixels=async()=>{
 const png=await page.locator('#canvas').screenshot();
 return page.evaluate(async uri=>{
   const bitmap=await createImageBitmap(await (await fetch(uri)).blob());
   const c=document.createElement('canvas');c.width=bitmap.width;c.height=bitmap.height;
   const ctx=c.getContext('2d');ctx.drawImage(bitmap,0,0);bitmap.close();
   return {gif:[...ctx.getImageData(56,266,1,1).data],controlledGif:[...ctx.getImageData(306,266,1,1).data]};
 },'data:image/png;base64,'+png.toString('base64'));
};
const pixel=(s,x,y)=>s.pixels[`${x},${y}`];
const near=(actual,expected,tolerance=2)=>{for(let i=0;i<4;i++)assert(Math.abs(actual[i]-expected[i])<=tolerance,`pixel ${actual} != ${expected}`);};
const at=(report,x,y)=>{
 const node=flat(report.tree).find(n=>n.tag==='image'&&n.x===x&&n.y===y);
 assert(node,`missing image at ${x},${y}`);return report.images.find(r=>r.node===node.node);
};
try {
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6096/',{waitUntil:'networkidle'});
 await wait(()=>window.probeResult || window.probeErrors?.length);
 assert.deepEqual(await page.evaluate(()=>window.probeErrors),[]);
 await wait(()=>{
  const r=JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[]));
  return r.images?.filter(i=>i.loaded).length===13 && r.images.filter(i=>i.failed).length===1;
 });
 await page.waitForTimeout(400);
 const report=await read(),shot=await snapshot();
 const gray=[80,80,80,255],red=[240,40,40,255],green=[40,200,60,255],blue=[40,80,240,255];
 for(let f=0;f<3;f++) {
  const x=20+f*250,tolerance=f===1?5:2;
  near(pixel(shot,x+50,78),gray);near(pixel(shot,x+50,157),gray);
  near(pixel(shot,x+10,118),red,tolerance);near(pixel(shot,x+50,118),green,tolerance);near(pixel(shot,x+90,118),blue,tolerance);
  near(pixel(shot,x+125+5,73),green,tolerance);near(pixel(shot,x+125+95,163),green,tolerance);
  const a=at(report,x,68),b=at(report,x+125,68);
  assert.equal(a.width,200);assert.equal(a.height,100);assert.equal(b.width,200);assert.equal(b.height,100);
 }
 checkpoints.staticFormats={passed:true,formats:['PNG','JPEG','WebP'],size:[200,100],aspectFit:'centered with letterboxing',aspectFill:'center crop fills bounds'};
 near(pixel(shot,525,280),gray);near(pixel(shot,615,280),gray);
 near(pixel(shot,570,240),blue);near(pixel(shot,570,280),green);near(pixel(shot,570,320),red);
 near(pixel(shot,650,235),green);near(pixel(shot,740,325),green);
 near(pixel(shot,430,265),[160,60,60,255],3);
 checkpoints.portraitAndAlpha={passed:true,alphaPixel:pixel(shot,430,265)};
 const labels=texts(report);
 assert.equal(labels.filter(t=>t==='200x100').length,6);
 assert(labels.some(t=>t.startsWith('Loads: 13 Errors: 1')&&t.endsWith('Calls: 13')));
 assert.deepEqual(report.errors,[]);
 checkpoints.events={passed:true,successfulLoads:13,decodeErrors:1,dimensionsReturnedToReact:true,mainThreadLoad:true};
 console.log('PASS: PNG/JPEG/WebP, landscape + portrait fit/fill, premultiplied alpha and React load/error events');
 // Observe actual GIF pixels, not just a scheduler counter.
 await page.waitForTimeout(700);
 const automaticBefore=await read();
 const automaticStatesBefore=await inspect();
 const animationPixels=[];
 for(let i=0;i<6;i++) {animationPixels.push((await compositorPixels()).gif);await page.waitForTimeout(110);}
 const automaticAfter=await read();
 assert(automaticAfter.frames>automaticBefore.frames+2,'GIF did not autonomously submit canvas frames');
 assert.deepEqual(texts(automaticAfter),texts(automaticBefore),'Another React update interfered with the autonomous GIF test');
 const infiniteBefore=automaticStatesBefore.find(r=>r.type==='image/gif'&&r.autoplay);
 const infiniteAfter=(await inspect()).find(r=>r.token===infiniteBefore.token);
 assert(infiniteAfter.uploads>infiniteBefore.uploads+2);
 checkpoints.autonomousFrames={passed:true,noForcedDraw:true,noReactUpdates:true,
   frames:[automaticBefore.frames,automaticAfter.frames],uploads:[infiniteBefore.uploads,infiniteAfter.uploads]};
 assert(animationPixels.some(p=>p[0]>200 && p[2]<100));assert(animationPixels.some(p=>p[2]>200 && p[0]<100));
 await wait(()=>window.lynxCanvas.lynxImages.inspect().some(r=>r.type==='image/webp'&&r.animated&&r.ended));
 const states=await inspect();
 await wait(()=>{
   const r=JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[]));
   const flatten=n=>[n,...n.children.flatMap(flatten)];
   return flatten(r.tree).some(n=>n.text?.includes('Loops: 2 Finals: 1 Starts: 1'));
 });
 const finite=states.find(r=>r.type==='image/webp'&&r.animated);
 assert.equal(finite.loops,2);assert.equal(finite.uploads,4);
 const paused=states.find(r=>r.type==='image/gif'&&!r.autoplay);
 assert.equal(paused.uploads,1);
 const finiteBefore=(await inspect()).find(r=>r.token===finite.token).uploads;
 await page.waitForTimeout(400);
 assert.equal((await inspect()).find(r=>r.token===finite.token).uploads,finiteBefore);
 assert.equal((await inspect()).find(r=>r.token===paused.token).uploads,1);
 await click(310,325);
 await wait(()=>window.lynxCanvas.lynxImages.inspect().filter(r=>r.type==='image/gif').every(r=>r.autoplay));
 await page.waitForTimeout(450);
 assert((await inspect()).find(r=>r.token===paused.token).uploads>1);
 await click(310,325);await page.waitForTimeout(100);
 const stopped=(await inspect()).find(r=>r.token===paused.token).uploads;
 await page.waitForTimeout(400);assert.equal((await inspect()).find(r=>r.token===paused.token).uploads,stopped);
 await page.evaluate(()=>{
   Object.defineProperty(document,'hidden',{value:true,configurable:true});
   document.dispatchEvent(new Event('visibilitychange'));
 });
 await page.waitForTimeout(100);
 const hidden=await inspect(),hiddenFrames=(await read()).frames;
 await page.waitForTimeout(500);
 assert.deepEqual(await inspect(),hidden);assert.equal((await read()).frames,hiddenFrames);
 await page.evaluate(()=>{delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));});
 await page.waitForTimeout(400);
 assert((await inspect()).find(r=>r.type==='image/gif'&&r.autoplay).uploads>hidden.find(r=>r.type==='image/gif'&&r.autoplay).uploads);
 checkpoints.animation={passed:true,hiddenPagePauseResume:true,gifPixels:animationPixels,webpLoops:finite.loops,webpUploads:finite.uploads,autoplayPauseResume:true};
 console.log('PASS: GIF pixel changes, animated WebP stops after two loops, autoplay pause/resume');
 // src replacement must create a new request and release the previous decoder.
 const old=at(await read(),20,385);
 const cancelled1=await page.evaluate(()=>window.lynxCanvas.lynxImages.stats.cancelled);
 await click(180,410);
 await page.waitForFunction(old=>{
  const r=JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[]));return r.images.some(i=>i.node===old.node&&i.token!==old.token&&i.loaded);
 },old);
 const replaced=at(await read(),20,385);assert.notEqual(replaced.token,old.token);
 assert.equal(await page.evaluate(()=>window.lynxCanvas.lynxImages.stats.cancelled),cancelled1+1);
 await click(310,410);await page.waitForFunction(t=>!window.lynxCanvas.lynxImages.inspect().some(r=>r.token===t),replaced.token);
 assert.equal((await read()).images.length,13);
 await click(310,410);await wait(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).images.length===14);
 checkpoints.lifecycle={passed:true,srcReplacement:true,removeAndReinsert:true};
 await page.screenshot({path:path.join(out,'browser.png')});
 await fs.writeFile(path.join(out,'canvas.png'),Buffer.from(shot.png.split(',')[1],'base64'));
 // Native rejection of stale completion and safe reload while GIF is running.
 const obsolete=(await read()).images.map(r=>r.token);
 await page.locator('#bundle-file').setInputFiles(imageBundle);
 await page.locator('#load-bundle').click();await wait(()=>document.querySelector('#load-status').dataset.state==='success');
 await wait(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).images.filter(i=>i.loaded).length===13);
 const reloaded=await read();assert.equal(reloaded.bundleSequence,2);
 assert(reloaded.images.every(i=>!obsolete.includes(i.token)));
 const errorsBefore=await page.evaluate(()=>window.lynxCanvas.lynxImages.stats.errors);
 await page.evaluate(t=>window.lynxCanvas.ccall('image_complete',null,['number','string'],[t,'stale decode']),obsolete[0]);
 assert.deepEqual((await read()).errors,[]);
 assert.equal(await page.evaluate(()=>window.lynxCanvas.lynxImages.stats.errors),errorsBefore);
 checkpoints.reload={passed:true,oldRequestsCancelled:true,staleCallbacksRejected:true};
 // Real network/CORS/decode failures and cancellation before a response arrives.
 let slowRequests=0;
 const releaseSlow=[];
 await page.route('**/fixtures/**',async route=>{
   const url=route.request().url();
   if(url.endsWith('/slow.png')) {
     slowRequests++;
     await new Promise(resolve=>releaseSlow.push(resolve));
     await route.fulfill({status:200,contentType:'image/png',body:bytes('png')}).catch(()=>{});
   } else if(url.endsWith('/not-found.png')) {
     await route.fulfill({status:404,body:'missing'});
   } else if(url.endsWith('/bad.png')) {
     await route.fulfill({status:200,contentType:'image/png',body:bytes('png').subarray(0,30)});
   } else if(url.endsWith('/animated.gif')) {
     await route.fulfill({status:200,contentType:'image/gif',body:bytes('gif')});
   } else await route.continue();
 });
 const lifecycle=async()=>{
   await page.locator('#bundle-file').setInputFiles(path.resolve('out/Wasm/image-lifecycle.lynx.bundle'));
   await page.locator('#load-bundle').click();
   await wait(()=>document.querySelector('#load-status').dataset.state==='success');
   await wait(()=>{
     const r=JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[]));
     return r.images.filter(i=>i.loaded).length===2 && r.images.filter(i=>i.failed).length===3;
   });
   await page.waitForTimeout(100);
 };
 await lifecycle();
 assert.equal(slowRequests,1);
 let network=await read();
 assert(texts(network).includes('Loads: 2 Errors: 3 Stale: 0'));
 const pending=at(network,20,20);assert(!pending.loaded&&!pending.failed);
 await click(70,200);
 await wait(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).images.filter(i=>i.loaded).length===3);
 releaseSlow.shift()();await page.waitForTimeout(200);
 network=await read();assert(texts(network).includes('Loads: 3 Errors: 3 Stale: 0'));
 assert(!network.images.some(i=>i.token===pending.token));
 const staleAllocation=await page.evaluate(t=>window.lynxCanvas.ccall('image_allocate','number',['number','number','number'],[t,200,100]),pending.token);
 assert.equal(staleAllocation,0);
 await lifecycle();assert.equal(slowRequests,2);
 const removed=at(await read(),20,20);
 await click(270,200);await page.waitForTimeout(100);releaseSlow.shift()();await page.waitForTimeout(200);
 assert(!(await read()).images.some(i=>i.token===removed.token));
 assert(texts(await read()).includes('Loads: 2 Errors: 3 Stale: 0'));
 await page.waitForTimeout(700);
 const idle=(await read()).frames;await page.waitForTimeout(400);assert.equal((await read()).frames,idle);
 await lifecycle();assert.equal(slowRequests,3);
 const tokens=(await read()).images.map(i=>i.token);
 await page.locator('#bundle-file').setInputFiles(imageBundle);
 await page.locator('#load-bundle').click();await wait(()=>document.querySelector('#load-status').dataset.state==='success');
 releaseSlow.shift()();
 await wait(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).images.filter(i=>i.loaded).length===13);
 assert((await read()).images.every(i=>!tokens.includes(i.token)));
 checkpoints.network={passed:true,http404:true,corruptPngDecode:true,corsAllowed:true,corsDenied:true,
   replacePending:true,removePending:true,reloadPending:true,staleTextureRejected:true,idleAfterFiniteAnimation:true};
 console.log('PASS: HTTP/CORS/decode failures, slow request replacement/removal/reload and idle rendering');
 // No ImageDecoder: browser bitmap fallback must decode GIF/WebP and render.
 const unsupported=await browser.newPage();
 await unsupported.addInitScript(()=>{Object.defineProperty(globalThis,'ImageDecoder',{value:undefined,configurable:true});});
 const unavailableErrors=[];unsupported.on('pageerror',e=>unavailableErrors.push(String(e)));
 await unsupported.goto(process.env.PROBE_URL || 'http://127.0.0.1:6096/');
 await unsupported.waitForFunction(()=>{
   if(!window.lynxCanvas)return false;
   const r=JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[]));
   return r.images.filter(i=>i.loaded).length===13 && r.images.filter(i=>i.failed).length===1;
 },null,{timeout:60000});
 assert.deepEqual(unavailableErrors,[]);assert.deepEqual(await unsupported.evaluate(()=>window.probeErrors),[]);
 const fallbackInitial=await unsupported.evaluate(()=>({frames:JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).frames,images:window.lynxCanvas.lynxImages.inspect()}));
 assert(fallbackInitial.images.filter(i=>i.animated).every(i=>i.backend==='browser-bitmap'));
 await unsupported.waitForTimeout(1200);
 const fallbackFinal=await unsupported.evaluate(()=>({frames:JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).frames,images:window.lynxCanvas.lynxImages.inspect()}));
 assert(fallbackFinal.frames>fallbackInitial.frames+2);
 assert(fallbackFinal.images.find(i=>i.type==='image/gif'&&i.autoplay).uploads>2);
 await unsupported.close();
 checkpoints.compatibility={passed:true,missingImageDecoder:'13 successful loads, animated GIF/WebP advance through browser-bitmap fallback',frames:[fallbackInitial.frames,fallbackFinal.frames]};
 // Visible but slow rAF must advance images instead of dropping every interval.
 const slow=await browser.newPage();
 await slow.addInitScript(()=>{
   window.requestAnimationFrame=cb=>setTimeout(()=>cb(performance.now()),300);
   window.cancelAnimationFrame=token=>clearTimeout(token);
 });
 await slow.goto(process.env.PROBE_URL || 'http://127.0.0.1:6096/');
 await slow.waitForFunction(()=>window.probeResult && window.lynxCanvas.lynxImages.inspect().filter(r=>r.ready).length===13,null,{timeout:60000});
 const slowRead=()=>slow.evaluate(()=>({
   frames:JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).frames,
   gif:window.lynxCanvas.lynxImages.inspect().find(r=>r.type==='image/gif'&&r.autoplay),errors:window.probeErrors,
 }));
 const slowBefore=await slowRead();await slow.waitForTimeout(2000);const slowAfter=await slowRead();
 assert(slowAfter.gif.uploads>slowBefore.gif.uploads+2,'300 ms rAF stopped GIF decoding');
 assert(slowAfter.frames>slowBefore.frames+2,'300 ms rAF stopped canvas submissions');
 assert.deepEqual(slowAfter.errors,[]);await slow.close();
 checkpoints.slowFrameClock={passed:true,intervalMilliseconds:300,
   frames:[slowBefore.frames,slowAfter.frames],uploads:[slowBefore.gif.uploads,slowAfter.gif.uploads]};
 // WebKit reports the deliberately denied CORS fixture as a page diagnostic
 // even though fetch rejection is caught and delivered through binderror.
 const expectedCors=isWebKit?errors.filter(e=>/^Fetch API cannot load http: ?\/+127\.0\.0\.1:6196\/no-cors\.png due to access control checks\.$/.test(e)):[];
 assert(expectedCors.length<=3);
 assert.deepEqual(errors.filter(e=>!expectedCors.includes(e)),[]);
 checkpoints.network.expectedBrowserCorsDiagnostics=expectedCors.length;
 assert.deepEqual(await page.evaluate(()=>window.probeErrors),[]);assert.deepEqual((await read()).errors,[]);
 checkpoints.gpu={passed:true,errors:[],peakPendingFrames:(await read()).peakPendingFrames};
 console.log('PASS: source/removal/reload lifecycle and stale callback protection; no native or WebGPU errors');
 await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:true,browser:await browser.version(),engine:isWebKit?'WebKit':'Chromium',checkpoints,report:await read(),browserImages:await inspect(),stats:await page.evaluate(()=>window.lynxCanvas.lynxImages.stats)},null,2));
} catch(error) {
 await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:false,error:String(error),stack:error.stack,checkpoints,report:await read().catch(()=>null)},null,2));
 await page.screenshot({path:path.join(out,'failure.png')});throw error;
} finally {await fs.writeFile(path.join(out,'console.json'),JSON.stringify(messages,null,2));await browser.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
