// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const out=path.resolve(process.env.PROBE_OUTPUT || 'out/Wasm/validation-interactive');
await fs.mkdir(out,{recursive:true});
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH,headless:true,args:['--enable-unsafe-webgpu']});
const page=await browser.newPage({viewport:{width:1000,height:1100}});
const errors=[], messages=[], requests=[];
page.on('pageerror',e=>errors.push(String(e)));
page.on('console',m=>{if(messages.length<800)messages.push({type:m.type(),text:m.text()});});
page.on('request',r=>requests.push(r.url()));
const read=()=>page.evaluate(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])));
const flatten=n=>[n,...n.children.flatMap(flatten)];
const texts=r=>flatten(r.tree).filter(n=>n.tag==='text').map(n=>n.text);
const click=async(x,y)=>{ const b=await page.locator('#canvas').boundingBox();await page.mouse.click(b.x+1+x*b.width/802,b.y+1+y*b.height/502); };
const waitText=text=>page.waitForFunction(text=>{
 const flat=n=>[n,...n.children.flatMap(flat)];
 const r=JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[]));
 return flat(r.tree).some(n=>n.text===text);
},text,{timeout:15000});
const snapshot=()=>page.evaluate(async()=>{
 window.pauseLynxFramePump=true;
 const m=window.lynxCanvas;
 await window.probeDevice.queue.onSubmittedWorkDone();
 m.ccall('release_completed_frames',null,[],[]);
 const id=m.ccall('render_frame','number',[],[]);
 const copy=document.createElement('canvas');copy.width=800;copy.height=500;
 const ctx=copy.getContext('2d');ctx.drawImage(document.querySelector('#canvas'),0,0);
 const pixel=(x,y)=>[...ctx.getImageData(x,y,1,1).data];
 const result={id,pixel:pixel(100,280),png:copy.toDataURL(),report:JSON.parse(m.ccall('scene_report','string',[],[]))};
 await window.probeDevice.queue.onSubmittedWorkDone();m.ccall('release_frames',null,['number'],[id]);
 window.pauseLynxFramePump=false;
 return result;
});
const checkpoints={};
try {
 await page.route('**/*',route=>route.request().resourceType()==='document'?route.continue():route.abort());
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6093/',{waitUntil:'networkidle'});
 await page.waitForFunction(()=>window.probeResult || window.probeErrors?.length,{timeout:60000});
 assert.deepEqual(await page.evaluate(()=>window.probeErrors),[]);
 await page.waitForTimeout(400);
 const initial=await read(); assert.deepEqual(initial.errors,[]);assert.equal(initial.backgroundRuntime,true);
 assert(texts(initial).includes('Count: 0'));
 const before=(await snapshot());assert.deepEqual(before.pixel,[23,74,72,255]);
 await click(100,280);
 await page.waitForFunction(()=>{
  const r=JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[]));const flat=n=>[n,...n.children.flatMap(flat)];
  return flat(r.tree).some(n=>n.background===0xff23765b);
 });
 const green=await snapshot();assert.deepEqual(green.pixel,[35,118,91,255]);
 await click(100,280);
 await page.waitForFunction(()=>{
  const r=JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[]));const flat=n=>[n,...n.children.flatMap(flat)];
  return flat(r.tree).some(n=>n.background===0xff174a48);
 });
 assert.deepEqual((await snapshot()).pixel,[23,74,72,255]);
 checkpoints.color={passed:true,before:before.pixel,after:green.pixel};console.log('PASS: native main-thread click changes GPU pixels');
 for(let i=1;i<=10;i++){await click(350,220);await waitText(`Count: ${i}`);}
 const counted=await read(); assert(texts(counted).includes('Count: 10'));assert.deepEqual(counted.errors,[]);
 checkpoints.counter={passed:true,count:10,eventCount:counted.eventCount};console.log('PASS: ordinary bindtap + useState reaches 10');
 const idle1=(await read()).frames;await page.waitForTimeout(600);const idle2=(await read()).frames;assert.equal(idle1,idle2);
 await click(620,215);await waitText('Stop animation');
 const animation1=await snapshot();
 await page.waitForTimeout(5000);
 const animation2=await snapshot();
 const phase=r=>Number(texts(r).find(t=>t.startsWith('Animation frame:')).split(': ')[1]);
 assert(phase(animation2.report)-phase(animation1.report)>60);
 assert(animation2.report.frames-animation1.report.frames>60);
 const dot=r=>flatten(r.tree).find(n=>n.tag==='view'&&n.width===20&&n.height===20);
 assert.notEqual(dot(animation1.report).x,dot(animation2.report).x);
 assert(animation2.report.peakPendingFrames<=2);
 await click(620,215);await waitText('Start animation');await page.waitForTimeout(200);
 const stopped=await read();await page.waitForTimeout(600);const still=await read();
 assert.equal(phase(stopped),phase(still));assert.equal(stopped.frames,still.frames);
 checkpoints.animation={passed:true,from:phase(animation1.report),to:phase(animation2.report),frames:animation2.report.frames-animation1.report.frames,dotFrom:dot(animation1.report).x,dotTo:dot(animation2.report).x,peakPending:still.peakPendingFrames,idleFrames:[idle1,idle2],stoppedFrames:[stopped.frames,still.frames]};
 console.log('PASS: continuous animation updates native layout; stop leaves no redraws');
 await fs.writeFile(path.join(out,'animation.png'),Buffer.from(animation2.png.split(',')[1],'base64'));
 await page.screenshot({path:path.join(out,'browser.png')});
 assert.deepEqual(await page.evaluate(()=>window.probeErrors),[]);assert.deepEqual((await read()).errors,[]);assert.deepEqual(errors,[]);assert.equal(requests.length,1);
 // Reload while an animation is active, then verify no stale callbacks leak.
 await click(620,215);await waitText('Stop animation');await page.waitForTimeout(100);
 await page.locator('#bundle-file').setInputFiles(path.resolve('platform/wasm/app/dist/main.lynx.bundle'));
 await page.locator('#load-bundle').click();await page.waitForFunction(()=>document.querySelector('#load-status').dataset.state==='success');
 await waitText('Count: 0');await waitText('Start animation');await page.waitForTimeout(250);
 const reloaded=await read();const reloadedFrames=reloaded.frames;await page.waitForTimeout(600);assert.equal((await read()).frames,reloadedFrames);
 await click(350,220);await waitText('Count: 1');assert.deepEqual((await read()).errors,[]);
 checkpoints.reload={passed:true,sequence:reloaded.bundleSequence,oldAnimationStopped:true};
 await page.locator('#bundle-file').setInputFiles({name:'bad.lynx.bundle',mimeType:'application/octet-stream',buffer:Buffer.from('invalid!')});
 await page.locator('#load-bundle').click();await page.waitForFunction(()=>document.querySelector('#load-status').dataset.state==='error');
 await click(350,220);await waitText('Count: 2');assert.deepEqual((await read()).errors,[]);
 checkpoints.invalidBundle={passed:true,previousPageStillInteractive:true};
 // A drag must cancel tap; browser CSS scaling must preserve canvas coordinates.
 const box=await page.locator('#canvas').boundingBox();
 await page.mouse.move(box.x+100,box.y+280);await page.mouse.down();
 await page.mouse.move(box.x+150,box.y+280,{steps:4});await page.mouse.up();
 await page.waitForTimeout(100);assert.deepEqual((await snapshot()).pixel,[23,74,72,255]);
 await page.locator('#canvas').evaluate(el=>{el.style.width='400px';el.style.height='250px';});
 await click(350,220);await waitText('Count: 3');
 await page.locator('#canvas').evaluate(el=>{el.style.width='800px';el.style.height='500px';});
 checkpoints.pointer={passed:true,dragDoesNotTap:true,scaledCoordinates:true};
 // Exercise the native cap and release only a completed prefix of the queue.
 const queue=await page.evaluate(async()=>{
   window.pauseLynxFramePump=true;const m=window.lynxCanvas;
   await window.probeDevice.queue.onSubmittedWorkDone();m.ccall('release_completed_frames',null,[],[]);
   const first=m.ccall('render_frame','number',[],[]),second=m.ccall('render_frame','number',[],[]),blocked=m.ccall('render_frame','number',[],[]);
   const full=JSON.parse(m.ccall('scene_report','string',[],[])).pendingFrames;
   await window.probeDevice.queue.onSubmittedWorkDone();
   m.ccall('release_frames',null,['number'],[first]);const partial=JSON.parse(m.ccall('scene_report','string',[],[])).pendingFrames;
   m.ccall('release_frames',null,['number'],[first]);const repeated=JSON.parse(m.ccall('scene_report','string',[],[])).pendingFrames;
   m.ccall('release_frames',null,['number'],[second]);const empty=JSON.parse(m.ccall('scene_report','string',[],[])).pendingFrames;
   window.pauseLynxFramePump=false;return {first,second,blocked,full,partial,repeated,empty};
 });
 assert(queue.first>0);assert(queue.second>queue.first);assert.equal(queue.blocked,-1);
 assert.deepEqual([queue.full,queue.partial,queue.repeated,queue.empty],[2,1,1,0]);
 checkpoints.gpuQueue={passed:true,...queue};

 assert.deepEqual(errors,[]);assert.deepEqual(await page.evaluate(()=>window.probeErrors),[]);
 await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:true,browser:browser.version(),checkpoints,requests,errors,messages},null,2));
 console.log(JSON.stringify({passed:true,checkpoints},null,2));
} catch(error) {
 await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:false,error:String(error),checkpoints,status:await page.locator('#status').textContent().catch(()=>null),report:await read().catch(()=>null),errors,messages},null,2));
 throw error;
} finally {await browser.close();}
