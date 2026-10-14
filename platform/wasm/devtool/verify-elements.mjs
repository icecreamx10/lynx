// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import fs from 'node:fs/promises';
const {chromium}=createRequire(import.meta.url)(process.env.PLAYWRIGHT_PATH || 'playwright');
const browser=await chromium.launch({headless:true,executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',args:['--enable-unsafe-webgpu']});
const page=await browser.newPage({viewport:{width:1300,height:1400}});const errors=[];page.on('pageerror',e=>errors.push(e.stack || String(e)));
try {
 await page.goto(process.env.PROBE_URL||'http://127.0.0.1:6140/');
 await page.waitForFunction(()=>window.probeResult||window.probeErrors?.length,null,{timeout:60000});
 assert.deepEqual(await page.evaluate(()=>probeErrors),[]);
 await page.evaluate(()=>{
  window.cdpEvents=[];window.cdpPending=new Map();window.cdpID=0;
  lynxCanvas.onDevToolMessage=raw=>{const r=JSON.parse(raw);if(r.id){const done=cdpPending.get(r.id);cdpPending.delete(r.id);done?.(r);}else cdpEvents.push(r);};
  window.cdp=(method,params={})=>new Promise(resolve=>{const id=++cdpID;cdpPending.set(id,resolve);lynxCanvas.ccall('devtool_dispatch',null,['string'],[JSON.stringify({id,method,params})]);});
 });
 const call=(m,p={})=>page.evaluate(([m,p])=>cdp(m,p),[m,p]);
 await call('DOM.enable');const doc=await call('DOM.getDocument');assert(!doc.error,JSON.stringify(doc));
 const flat=n=>[n,...(n.children||[]).flatMap(flat)];const nodes=flat(doc.result.root);assert(nodes.some(n=>n.nodeName==='view'));assert(nodes.some(n=>n.nodeType===3));
 const card=nodes.find(n=>n.nodeName==='view'&&n.childNodeCount>0&&n!==doc.result.root.children[0]);assert(card);
 const box=await call('DOM.getBoxModel',{nodeId:card.nodeId});assert(box.result.model.width>0);
 const initial=await call('CSS.getComputedStyleForNode',{nodeId:card.nodeId});assert(initial.result.computedStyle.length>=5);
 await call('CSS.enable');const inline=(await call('CSS.getInlineStylesForNode',{nodeId:card.nodeId})).result.inlineStyle;
 const edited=await call('CSS.setStyleTexts',{edits:[{styleSheetId:inline.styleSheetId,range:inline.range,text:inline.cssText+';background-color: #ff0000;'}]});assert(!edited.error,JSON.stringify(edited));
 const after=await call('CSS.getComputedStyleForNode',{nodeId:card.nodeId});assert(after.result.computedStyle.find(s=>s.name==='background-color').value.startsWith('rgba(255,0,0,'),JSON.stringify(after));
 await call('Overlay.highlightNode',{nodeId:card.nodeId});await page.waitForTimeout(100);await call('Overlay.hideHighlight');
 const bad=await call('CSS.setStyleTexts',{edits:[{styleSheetId:'inline.9999999',range:{},text:'color:red'}]});assert(bad.error);
 await page.locator('#toggle-devtool').click();await page.waitForFunction(()=>lynxDevTool.attached,null,{timeout:60000});
 await page.waitForTimeout(1200);const frame=page.frames().find(f=>f.url().includes('/devtools/inspector.html'));assert(frame);
 await fs.mkdir('out/Wasm/validation-devtool-elements',{recursive:true});await page.screenshot({path:'out/Wasm/validation-devtool-elements/browser.png'});
 const checks={realNativeNodes:nodes.length,computedStyles:true,inlineEdit:true,highlight:true,staleNodeRejected:true,officialFrontendAttached:true,errors};
 assert.deepEqual(errors,[]);
 await fs.writeFile('out/Wasm/validation-devtool-elements/validation.json',JSON.stringify({passed:true,checks,doc},null,2));console.log(checks);
} finally {await browser.close();}
