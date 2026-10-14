// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import fs from 'node:fs/promises';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const out=process.env.PROBE_OUT || 'out/Wasm/validation-devtool-native';
await fs.mkdir(out,{recursive:true});
const browser=await chromium.launch({headless:true,
 executablePath:process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
 args:['--enable-unsafe-webgpu']});
const page=await browser.newPage({viewport:{width:1500,height:1400}});
const errors=[],checks={};
page.on('pageerror',error=>errors.push(String(error)));
const flat=node=>[node,...(node.children||[]).flatMap(flat)];
try {
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6140/',{waitUntil:'load'});
 await page.waitForFunction(()=>window.probeResult || window.probeErrors?.length,null,{timeout:60000});
 assert.deepEqual(await page.evaluate(()=>probeErrors),[]);
 // Use the unmodified protocol agents through the browser transport.
 await page.evaluate(async()=>{
  const module=lynxCanvas;
  window.cdpMessages=[];
  window.cdpPending=new Map();
  let next=1;
  module.onLynxCDP=(view,type,payload)=>{
   if(type!=='CDP'||view!==window.cdpView)return;
   const message=JSON.parse(payload);cdpMessages.push(message);
   const pending=cdpPending.get(message.id);
   if(pending){clearTimeout(pending.timer);cdpPending.delete(message.id);pending.resolve(message);}
  };
  window.cdpView=await module.call('devtool_attach');
  window.cdpSend=(method,params={},sessionId)=>new Promise((resolve,reject)=>{
   const id=next++;
   const timer=setTimeout(()=>{cdpPending.delete(id);reject(Error('CDP timed out: '+method));},15000);
   cdpPending.set(id,{resolve,reject,timer});
   module.call('devtool_dispatch',[
    cdpView,JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})})]);
  });
 });
 const send=(method,params={},sessionId)=>page.evaluate(
  ([m,p,s])=>cdpSend(m,p,s),[method,params,sessionId]);
 const ok=async(method,params={},sessionId)=>{
  const response=await send(method,params,sessionId);
  assert(!response.error,method+': '+JSON.stringify(response.error));
  return response.result;
 };
 await ok('DOM.enable');
 const document=await ok('DOM.getDocument',{depth:-1});
 const counter=flat(document.root).find(n=>n.attributes?.includes('counter'));
 assert(counter,'Real Lynx counter is absent from the native DOM');
 checks.nativeDOM={nodeId:counter.nodeId,nodes:flat(document.root).length};
 await ok('CSS.enable');
 checks.inlineStyle=await ok('CSS.getInlineStylesForNode',{nodeId:counter.nodeId});
 checks.computedStyle=(await ok('CSS.getComputedStyleForNode',{nodeId:counter.nodeId})).computedStyle.length;
 const pixel=()=>page.evaluate(async id=>{
  const state=await lynxCanvas.call('state');
  const flatten=n=>[n,...n.children.flatMap(flatten)];
  const node=flatten(state.report.tree).find(n=>n.node===id);
  const snapshot=await lynxCanvas.call('snapshot',[[[node.x+node.width-10,node.y+node.height-10]]]);
  return snapshot.pixels[0];
 },counter.nodeId);
 const before=await pixel();
 await ok('Overlay.highlightNode',{nodeId:counter.nodeId,highlightConfig:{contentColor:{r:255,g:0,b:0,a:1}}});
 assert.notDeepEqual(await pixel(),before);checks.nativeHighlight=true;
 await ok('Overlay.hideHighlight');
 assert.deepEqual(await pixel(),before);
 const inline=checks.inlineStyle.inlineStyle;
 await ok('CSS.setStyleTexts',{edits:[{styleSheetId:inline.styleSheetId,range:inline.range,text:'background-color: #23765b;'}]});
 assert.deepEqual((await pixel()).slice(0,3),[35,118,91]);checks.cssPixels=true;
 await ok('Runtime.enable');
 await ok('Debugger.enable');
 await page.waitForFunction(()=>cdpMessages.some(m=>m.method==='Debugger.scriptParsed'&&m.params.url.endsWith('background.js')));
 const evaluation=await ok('Runtime.evaluate',{expression:'6*7',returnByValue:true});
 assert.equal(evaluation.result.value,42);checks.evaluate=42;
 const mts=await ok('Runtime.evaluate',{expression:'6*7',returnByValue:true},'Main');
 assert.equal(mts.result.value,42);checks.mtsEvaluate=42;
 await ok('Runtime.evaluate',{expression:"console.log('browser-inspector-test', {answer:42})"});
 await page.waitForFunction(()=>cdpMessages.some(m=>m.method==='Runtime.consoleAPICalled'&&m.params.args?.some(a=>a.value==='browser-inspector-test')));
 checks.nativeConsole=true;
 const script=await page.evaluate(()=>cdpMessages.find(m=>m.method==='Debugger.scriptParsed'&&m.params.url.endsWith('background.js')).params);
 const {scriptSource}=await ok('Debugger.getScriptSource',{scriptId:script.scriptId});
 const line=scriptSource.split('\n').findIndex(l=>l.includes('next = value + 1'));
 assert(line>=0,'Debugger fixture source line was not exposed by Inspector');
 const breakpoint=await ok('Debugger.setBreakpointByUrl',{url:script.url,lineNumber:line});
 assert(breakpoint.locations.length>0,'Breakpoint did not resolve');
 await page.evaluate(()=>{
  window.breakpointResult=undefined;window.breakpointError=undefined;
  cdpSend('Runtime.evaluate',{expression:'__lynxInspectorIncrement(41)',returnByValue:true})
   .then(r=>window.breakpointResult=r).catch(e=>window.breakpointError=String(e));
 });
 await page.waitForFunction(()=>cdpMessages.some(m=>m.method==='Debugger.paused'),null,{timeout:15000});
 const paused=await page.evaluate(()=>cdpMessages.findLast(m=>m.method==='Debugger.paused').params);
 assert(paused.callFrames.length>0);
 const local=await ok('Debugger.evaluateOnCallFrame',{callFrameId:paused.callFrames[0].callFrameId,expression:'value',returnByValue:true});
 assert.equal(local.result.value,41);
 checks.breakpoint={source:script.url,line,local:41,callFrames:paused.callFrames.length};
 await ok('Debugger.stepOver');
 await page.waitForFunction(()=>cdpMessages.filter(m=>m.method==='Debugger.paused').length>=2);
 const stepped=await page.evaluate(()=>cdpMessages.findLast(m=>m.method==='Debugger.paused').params);
 const next=await ok('Debugger.evaluateOnCallFrame',{callFrameId:stepped.callFrames[0].callFrameId,expression:'next',returnByValue:true});
 assert.equal(next.result.value,42);checks.stepOver=42;
 await ok('Debugger.resume');
 await page.waitForFunction(()=>window.breakpointResult || window.breakpointError);
 const resumed=await page.evaluate(()=>breakpointResult);
 assert.equal(resumed.result.result.value,42);checks.resume=42;
 // Re-enter the same breakpoint and detach while the native queue is paused.
 await page.evaluate(()=>{
  cdpMessages.length=0;window.detachedResult=undefined;
  cdpSend('Runtime.evaluate',{expression:'__lynxInspectorIncrement(9)',returnByValue:true})
    .then(r=>window.detachedResult=r).catch(()=>{});
 });
 await page.waitForFunction(()=>cdpMessages.some(m=>m.method==='Debugger.paused'));
 await page.evaluate(()=>lynxCanvas.call('devtool_detach',[cdpView]));
 await page.evaluate(async()=>{cdpView=await lynxCanvas.call('devtool_attach');});
 const afterDetach=await ok('Runtime.evaluate',{expression:'__lynxInspectorIncrement(9)',returnByValue:true});
 assert.equal(afterDetach.result.value,10);
 checks.detachWhilePaused=true;
 await page.evaluate(()=>lynxCanvas.call('devtool_detach',[cdpView]));
 // The actual official frontend must populate its Elements view.
 const frameReady=page.waitForEvent('framenavigated',{predicate:f=>f.url().includes('/devtools/inspector.html')});
 await page.locator('#toggle-devtool').click();
 const frame=await frameReady;
 await frame.locator('.elements-tree-outline').waitFor({timeout:30000});
 await page.screenshot({path:out+'/frontend.png',fullPage:true});
 await frame.getByRole('tab',{name:'Sources',exact:true}).click();
 await page.keyboard.press('Meta+p');
 const prompt=frame.locator('.filtered-list-widget [contenteditable]').first();
 await prompt.fill('devtool.tsx');
 await frame.locator('.filtered-list-widget-item').filter({hasText:'devtool.tsx'}).first().waitFor();
 await prompt.press('Enter');
 await frame.locator('.tabbed-pane-header-tab').filter({hasText:'devtool.tsx'}).first().waitFor();
 const editor=frame.locator('.CodeMirror-code, .cm-content').first();
 await editor.waitFor();
 assert((await editor.textContent()).includes('inspectableIncrement'));
 checks.officialSources=true;
 await page.screenshot({path:out+'/sources.png',fullPage:true});

 checks.officialFrontend=true;
 await page.locator('#toggle-devtool').click();
 assert.equal(await page.locator('#devtool-panel iframe').count(),0);
 checks.close=true;
 // Replace the inspected bundle and verify the new native view still works.
 await page.locator('#bundle-file').setInputFiles('platform/wasm/app/dist/main.lynx.bundle');
 await page.locator('#load-bundle').click();
 await page.waitForFunction(()=>document.querySelector('#load-status').dataset.state==='success');
 const clickPoint=await page.evaluate(()=>{
  const flatten=n=>[n,...n.children.flatMap(flatten)];
  const report=window.probeResult;
  const label=flatten(report.tree).find(n=>n.text==='Count: 0');
  const rect=document.querySelector('#canvas').getBoundingClientRect();
  return {x:rect.x+1+(label.x+5)*rect.width/802,y:rect.y+1+(label.y+5)*rect.height/502};
 });
 await page.mouse.click(clickPoint.x,clickPoint.y);
 await page.waitForFunction(()=>{
  const flatten=n=>[n,...n.children.flatMap(flatten)];
  const report=window.probeResult;
  return flatten(report.tree).some(n=>n.text==='Count: 1');
 });
 checks.reloadAndClick=true;
 assert.deepEqual(errors,[]);
 assert.deepEqual(await page.evaluate(()=>probeErrors),[]);
 await fs.rm(out+'/failure.json',{force:true});
 await fs.rm(out+'/failure.png',{force:true});
 await fs.writeFile(out+'/validation.json',JSON.stringify({passed:true,checks,errors},null,2));
 console.log(JSON.stringify({passed:true,checks},null,2));
} catch(error) {
 await page.screenshot({path:out+'/failure.png',fullPage:true}).catch(()=>{});
 await fs.writeFile(out+'/failure.json',JSON.stringify({error:String(error),checks,errors,
  messages:await page.evaluate(()=>window.cdpMessages||[]).catch(()=>[])},null,2));
 throw error;
} finally {await browser.close();}
