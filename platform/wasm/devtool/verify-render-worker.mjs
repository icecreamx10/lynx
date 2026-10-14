// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
// Build the existing interactive and images fixtures before running this gate.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const out=process.env.PROBE_OUT || 'out/Wasm/validation-render-threading';
await fs.mkdir(out,{recursive:true});
const browser=await chromium.launch({headless:true,
 executablePath:process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
 args:['--enable-unsafe-webgpu']});
const errors=[],checks={};
const page=await browser.newPage({viewport:{width:1200,height:1300}});
page.on('pageerror',error=>errors.push(String(error)));
const flat=n=>[n,...(n.children||[]).flatMap(flat)];
const read=()=>page.evaluate(()=>lynxCanvas.call('state'));
const sample=points=>page.evaluate(points=>lynxCanvas.call('snapshot',[points]),points);
const click=async(x,y)=>{const b=await page.locator('#canvas').boundingBox();await page.mouse.click(b.x+1+x*b.width/802,b.y+1+y*b.height/502);};
const waitText=text=>page.waitForFunction(text=>{
 const flat=n=>[n,...n.children.flatMap(flat)];return flat(probeResult.tree).some(n=>n.text===text);
},text);
const load=async path=>{
 await page.locator('#bundle-file').setInputFiles(path);await page.locator('#load-bundle').click();
 await page.waitForFunction(()=>document.querySelector('#load-status').dataset.state==='success');
};
const send=(method,params={},sessionId)=>page.evaluate(([m,p,s])=>cdpSend(m,p,s),[method,params,sessionId]);
const ok=async(...args)=>{const r=await send(...args);assert(!r.error,JSON.stringify(r));return r.result;};
const pauses=()=>page.evaluate(()=>cdpMessages.filter(m=>m.method==='Debugger.paused'&&m.sessionId==='Main').length);
const waitPause=n=>page.waitForFunction(n=>cdpMessages.filter(m=>m.method==='Debugger.paused'&&m.sessionId==='Main').length>n,n,{timeout:15000});
const paused=()=>page.evaluate(()=>cdpMessages.findLast(m=>m.method==='Debugger.paused'&&m.sessionId==='Main').params);
try {
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6144/');
 await page.waitForFunction(()=>window.probeResult||window.probeErrors?.length,null,{timeout:60000});
 assert.deepEqual(await page.evaluate(()=>probeErrors),[]);
 await page.getByRole('button',{name:'关闭 Lynx DevTool',exact:true}).waitFor();
 const defaultFrame=await page.locator('#devtool-panel iframe').elementHandle().then(h=>h.contentFrame());
 await defaultFrame.locator('.elements-tree-outline').waitFor();
 await page.locator('#toggle-devtool').click();
 await page.waitForFunction(()=>probeResult.backgroundRuntime);
 const initial=(await read()).report;
 assert.equal(initial.renderWorker,true);
 assert.notEqual(initial.browserThread,initial.renderThread);
 assert.notEqual(initial.btsThread,initial.renderThread);
 assert.notEqual(initial.btsThread,initial.browserThread);
 checks.ownership={browser:initial.browserThread,render:initial.renderThread,bts:initial.btsThread};
 await page.evaluate(async()=>{
  window.cdpMessages=[];window.cdpPending=new Map();let id=1;
  lynxCanvas.onLynxCDP=(view,type,payload)=>{
   if(type!=='CDP'||view!==cdpView)return;
   const m=JSON.parse(payload);cdpMessages.push(m);
   const p=cdpPending.get(m.id);if(p){clearTimeout(p.timer);cdpPending.delete(m.id);p.resolve(m);}
  };
  window.cdpView=await lynxCanvas.call('devtool_attach');
  window.cdpSend=(method,params={},sessionId)=>new Promise((resolve,reject)=>{
   const current=id++;const timer=setTimeout(()=>{cdpPending.delete(current);reject(Error(method+' timed out'));},15000);
   cdpPending.set(current,{resolve,reject,timer});
   lynxCanvas.call('devtool_dispatch',[cdpView,JSON.stringify({id:current,method,params,...(sessionId?{sessionId}:{})})]);
  });
  window.heartbeat=0;setInterval(()=>++window.heartbeat,10);
 });
 await ok('Runtime.enable',{},'Main');await ok('Debugger.enable',{},'Main');
 const expression='globalThis.inspectMTS = function inspectMTS(value) {\n const next = value + 1;\n const doubled = next * 2;\n return doubled;\n};\n//# sourceURL=wasm-mts-acceptance.js\n';
 await ok('Runtime.evaluate',{expression,objectGroup:'console'},'Main');
 const bp=await ok('Debugger.setBreakpointByUrl',{url:'wasm-mts-acceptance.js',lineNumber:1},'Main');
 assert(bp.locations.length>0);
 const trigger=()=>page.evaluate(()=>{
  window.mtsResult=undefined;window.mtsError=undefined;
  cdpSend('Runtime.evaluate',{expression:'inspectMTS(41)',returnByValue:true},'Main')
    .then(r=>window.mtsResult=r).catch(e=>window.mtsError=String(e));
 });
 await trigger();await waitPause(0);
 const first=await paused();
 assert.equal((await ok('Debugger.evaluateOnCallFrame',{callFrameId:first.callFrames[0].callFrameId,expression:'value',returnByValue:true},'Main')).result.value,41);
 const heartbeat=await page.evaluate(()=>window.heartbeat);
 await page.waitForTimeout(300);
 assert(await page.evaluate(()=>window.heartbeat)>heartbeat+10,'Window was blocked by MTS pause');
 assert.equal((await ok('Runtime.evaluate',{expression:'6*7',returnByValue:true})).result.value,42,'BTS must remain runnable');
 // Rendering work must stay queued until the owning MTS thread resumes.
 await page.evaluate(()=>{window.hostReplied=false;lynxCanvas.call('state').then(()=>window.hostReplied=true);});
 await page.waitForTimeout(100);assert.equal(await page.evaluate(()=>hostReplied),false);
 await ok('Debugger.stepOver',{},'Main');await waitPause(1);
 const step=await paused();
 assert.notEqual(step.callFrames[0].location.lineNumber,first.callFrames[0].location.lineNumber);
 assert.equal((await ok('Debugger.evaluateOnCallFrame',{callFrameId:step.callFrames[0].callFrameId,expression:'next',returnByValue:true},'Main')).result.value,42);
 await ok('Debugger.resume',{},'Main');
 await page.waitForFunction(()=>window.mtsResult&&window.hostReplied);
 assert.equal(await page.evaluate(()=>mtsResult.result.result.value),84);
 checks.mtsBreakpoint={resolved:true,local:41,stepLocal:42,result:84,windowResponsive:true,btsRunnable:true,renderQueued:true};
 const n=await pauses();await trigger();await waitPause(n);
 await page.evaluate(()=>lynxCanvas.call('devtool_detach',[cdpView]));
 await read();
 await page.evaluate(async()=>{cdpView=await lynxCanvas.call('devtool_attach');});
 assert.equal((await ok('Runtime.evaluate',{expression:'inspectMTS(9)',returnByValue:true},'Main')).result.value,20);
 checks.mtsDetachResumes=true;
 await page.evaluate(()=>lynxCanvas.call('devtool_detach',[cdpView]));
 await load('platform/wasm/app/dist/main.lynx.bundle');
 await waitText('Count: 0');
 assert.deepEqual((await sample([[100,280]])).pixels[0],[23,74,72,255]);
 // Pause the real compiled main-thread tap handler through the same Inspector.
 await page.evaluate(async()=>{cdpMessages.length=0;cdpView=await lynxCanvas.call('devtool_attach');});
 await ok('Debugger.enable',{},'Main');
 await page.waitForFunction(()=>cdpMessages.some(m=>m.method==='Debugger.scriptParsed'&&m.sessionId==='Main'&&m.params.url.endsWith('/main-thread.js')&&m.params.length>1000));
 const mtsScript=await page.evaluate(()=>cdpMessages.find(m=>m.method==='Debugger.scriptParsed'&&m.sessionId==='Main'&&m.params.url==='file:///main-thread.js'&&m.params.length>1000).params);
 const mtsSource=(await ok('Debugger.getScriptSource',{scriptId:mtsScript.scriptId},'Main')).scriptSource;
 const tapLine=mtsSource.split('\n').findIndex(line=>line.includes('var next = !'));
 assert(tapLine>=0,'Compiled MTS fixture source missing');
 const tapBreakpoint=await ok('Debugger.setBreakpointByUrl',{url:mtsScript.url,lineNumber:tapLine},'Main');
 assert(tapBreakpoint.locations.length>0);
 await click(100,280);await waitPause(0);
 const tapPause=await paused();assert(tapPause.callFrames.length>0);
 const tapCount=await pauses();await ok('Debugger.stepInto',{},'Main');await waitPause(tapCount);
 await ok('Debugger.resume',{},'Main');
 await page.waitForFunction(()=>{const f=n=>[n,...n.children.flatMap(f)];return f(probeResult.tree).some(n=>n.background===0xff23765b);});
 assert.deepEqual((await sample([[100,280]])).pixels[0],[35,118,91,255]);
 checks.compiledMtsTap={pause:true,stepInto:true,resumedPixels:[35,118,91,255]};
 await page.evaluate(()=>lynxCanvas.call('devtool_detach',[cdpView]));
 await load(process.env.INTERACTIVE_BUNDLE || 'out/Wasm/interactive.lynx.bundle');
 await waitText('Count: 0');
 for(let n=1;n<=5;n++){await click(350,220);await waitText('Count: '+n);}
 const idle=(await read()).report.frames;await page.waitForTimeout(500);assert.equal((await read()).report.frames,idle);
 await click(620,215);await waitText('Stop animation');
 const before=(await read()).report;await page.waitForTimeout(3000);const after=(await read()).report;
 assert(after.frames-before.frames>60);assert(after.peakPendingFrames<=2);
 const phase=r=>Number(flat(r.tree).find(n=>n.text?.startsWith('Animation frame:')).text.split(': ')[1]);
 assert(phase(after)-phase(before)>60);
 await click(620,215);await waitText('Start animation');await page.waitForTimeout(200);
 const stopped=(await read()).report.frames;await page.waitForTimeout(500);assert.equal((await read()).report.frames,stopped);
 checks.animation={frames:after.frames-before.frames,stateUpdates:phase(after)-phase(before),peak:after.peakPendingFrames,idleStops:true};
 await click(620,215);await waitText('Stop animation');
 await load(process.env.IMAGES_BUNDLE || 'out/Wasm/images.lynx.bundle');
 await page.waitForFunction(()=>probeResult.images?.filter(i=>i.loaded).length===13&&probeResult.images.filter(i=>i.failed).length===1,null,{timeout:30000});
 const imgs=await sample([[70,78],[30,118],[70,118],[110,118],[430,265]]);
 assert.deepEqual(imgs.pixels.slice(0,4),[[80,80,80,255],[240,40,40,255],[40,200,60,255],[40,80,240,255]]);
 assert(imgs.pixels[4].every((v,i)=>Math.abs(v-[160,60,60,255][i])<=3));
 const imageState=await read();await page.waitForTimeout(600);const imageAfter=await read();
 assert(imageAfter.imageStats.uploads>imageState.imageStats.uploads);
 assert.equal(imageAfter.report.pendingCallbacks,0);
 checks.images={loaded:13,expectedDecodeError:1,staticPixels:true,mtsLoadAlpha:true,animatedUploads:imageAfter.imageStats.uploads-imageState.imageStats.uploads};
 await page.screenshot({path:out+'/images.png'});
 await load(process.env.INTERACTIVE_BUNDLE || 'out/Wasm/interactive.lynx.bundle');
 await waitText('Count: 0');await click(350,220);await waitText('Count: 1');
 assert.deepEqual((await read()).report.errors,[]);assert.deepEqual(await page.evaluate(()=>probeErrors),[]);
 // The UI selector reloads into the retained browser-main-thread path.
 await page.locator('#thread-mode').selectOption('main');
 await page.waitForFunction(()=>window.probeResult?.renderWorker===false,null,{timeout:60000});
 const main=(await read()).report;assert.equal(main.renderThread,main.browserThread);
 await load(process.env.INTERACTIVE_BUNDLE || 'out/Wasm/interactive.lynx.bundle');
 await waitText('Count: 0');await click(100,280);
 await page.waitForFunction(()=>{const f=n=>[n,...n.children.flatMap(f)];return f(probeResult.tree).some(n=>n.background===0xff23765b);});
 await click(350,220);await waitText('Count: 1');
 checks.mainOption={selected:true,sameThread:true,mtsTap:true,btsCounter:true};
 assert.deepEqual(errors,[]);assert.deepEqual(await page.evaluate(()=>probeErrors),[]);
 await fs.writeFile(out+'/validation.json',JSON.stringify({passed:true,browser:browser.version(),checks,errors},null,2));
 console.log(JSON.stringify({passed:true,checks},null,2));
} catch(error) {
 await fs.writeFile(out+'/validation.json',JSON.stringify({passed:false,error:String(error),checks,errors,messages:await page.evaluate(()=>window.cdpMessages||[]).catch(()=>[])},null,2));
 await page.screenshot({path:out+'/failure.png'}).catch(()=>{});throw error;
} finally {await browser.close();}
