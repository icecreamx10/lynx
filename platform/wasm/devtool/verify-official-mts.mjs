// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import fs from 'node:fs/promises';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const out=process.env.PROBE_OUT || 'out/Wasm/validation-official-mts';
await fs.mkdir(out,{recursive:true});
const browser=await chromium.launch({headless:true,
 executablePath:process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
 args:['--enable-unsafe-webgpu']});
const page=await browser.newPage({viewport:{width:1500,height:1700}}),errors=[],checks={};
page.on('pageerror',error=>errors.push(String(error)));
try {
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6144/');
 await page.waitForFunction(()=>window.probeResult?.backgroundRuntime,null,{timeout:60000});
 await page.locator('#devtool-panel iframe').waitFor();
 const frame=await page.locator('#devtool-panel iframe').elementHandle().then(h=>h.contentFrame());
 checks.defaultOpen=true;
 const preview=await page.locator('.preview-pane').boundingBox();
 const panel=await page.locator('#devtool-controls').boundingBox();
 assert(panel.x>=preview.x+preview.width,'Wide layout must place DevTool beside bundle');
 checks.wideSideBySide=true;
 await frame.locator('.elements-tree-outline').waitFor();
 // Use the official frontend SDK to establish one reproducible source breakpoint.
 // Resume/step and disconnect below are exercised through its visible UI.
 await frame.evaluate(async()=>{
  const entry=document.querySelector('script[type=module]').src;
  window.SDK=await import(new URL('../../core/sdk/sdk.js',entry).href);
 });
 await frame.waitForFunction(()=>SDK.TargetManager.TargetManager.instance().models(SDK.DebuggerModel.DebuggerModel)
  .some(m=>m.target().id()==='Main'&&m.scripts().some(s=>s.sourceURL==='file:///main-thread.js')));
 const bp=await frame.evaluate(async()=>{
  window.mtsModel=SDK.TargetManager.TargetManager.instance().models(SDK.DebuggerModel.DebuggerModel).find(m=>m.target().id()==='Main');
  const script=mtsModel.scripts().find(s=>s.sourceURL==='file:///main-thread.js');
  const {scriptSource}=await mtsModel.target().debuggerAgent().invoke_getScriptSource({scriptId:script.scriptId});
  const lineNumber=scriptSource.split('\n').findIndex(l=>l.includes('var next = !'));
  return mtsModel.target().debuggerAgent().invoke_setBreakpointByUrl({url:script.sourceURL,lineNumber});
 });
 assert(bp.locations.length>0);
 const b=await page.locator('#canvas').boundingBox();
 const tap=()=>page.mouse.click(b.x+1+100*b.width/802,b.y+1+280*b.height/502);
 await tap();await frame.waitForFunction(()=>mtsModel.isPaused());
 await frame.getByRole('tab',{name:'Sources',exact:true}).click();
 const location=await frame.evaluate(()=>mtsModel.debuggerPausedDetails().callFrames[0].location().lineNumber);
 await frame.getByRole('button',{name:'Step over next function call',exact:true}).click();
 await frame.waitForFunction(line=>mtsModel.isPaused()&&mtsModel.debuggerPausedDetails().callFrames[0].location().lineNumber!==line,location);
 checks.stepButton=true;
 // Verify source-mapped TSX is available alongside the paused scope/call stack.
 await page.keyboard.press('Meta+p');
 const prompt=frame.locator('.filtered-list-widget [contenteditable]').first();await prompt.fill('devtool.tsx');
 await frame.locator('.filtered-list-widget-item').filter({hasText:'devtool.tsx'}).first().waitFor();await prompt.press('Enter');
 const editor=frame.locator('.CodeMirror-code, .cm-content').first();await editor.waitFor();
 assert((await editor.textContent()).includes('inspectableMainThreadTap'));
 await page.screenshot({path:out+'/paused.png',fullPage:true});checks.mappedMtsSource=true;
 await frame.getByRole('button',{name:'Resume script execution',exact:true}).click();
 await frame.waitForFunction(()=>!mtsModel.isPaused());
 await page.waitForFunction(()=>{const f=n=>[n,...n.children.flatMap(f)];return f(probeResult.tree).some(n=>n.background===0xff23765b);});
 checks.resumeButton=true;
 await tap();await frame.waitForFunction(()=>mtsModel.isPaused());
 await page.locator('#toggle-devtool').click();
 await page.evaluate(()=>lynxCanvas.call('state'));
 await page.waitForFunction(()=>{const f=n=>[n,...n.children.flatMap(f)];return f(probeResult.tree).some(n=>n.background===0xff174a48);});
 checks.closeWhilePaused=true;
 assert.deepEqual(errors,[]);assert.deepEqual(await page.evaluate(()=>probeErrors),[]);
 await fs.writeFile(out+'/validation.json',JSON.stringify({passed:true,checks,errors},null,2));
 console.log(JSON.stringify({passed:true,checks}));
} catch(error) {
 await page.screenshot({path:out+'/failure.png',fullPage:true}).catch(()=>{});
 await fs.writeFile(out+'/validation.json',JSON.stringify({passed:false,error:String(error),checks,errors},null,2));throw error;
} finally {await browser.close();}
