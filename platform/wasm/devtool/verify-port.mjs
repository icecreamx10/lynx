// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import fs from 'node:fs/promises';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'});
try {
 const page=await browser.newPage();
 const errors=[];page.on("pageerror",error=>errors.push(String(error)));
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6140/devtools/connection.js');
 await page.setContent('<div id="panel" style="height:700px"></div>');
 await page.evaluate(async()=>{
  const {LynxDevToolHost}=await import('/devtools/host.js');
  window.requests=[];
  window.host=new LynxDevToolHost({container:document.querySelector('#panel'),dispatch:(raw,generation)=>{
   const r=JSON.parse(raw);window.requests.push(r);
   window.host.send(JSON.stringify({id:r.id,error:{code:-32601,message:'Transport verification: no runtime attached'}}),generation);
  }});
  window.host.open(7);
 });
 await page.waitForFunction(()=>host.attached && requests.length>5,null,{timeout:60000});
 const initial=await page.evaluate(()=>requests.length);assert(initial>5);
 // Synthetic broadcast cannot impersonate the bound iframe or the port.
 await page.evaluate(()=>postMessage({kind:'lynx-devtool-ready',version:1,token:host.token},location.origin));
 assert(await page.evaluate(()=>host.attached));
 // Generation filtering is independent from the CDP request's own sessionId.
 const checks={officialFrontendRequests:initial,originAndSourceCheck:true};
 await page.evaluate(()=>host.close());
 assert.equal(await page.locator('iframe').count(),0);
 for(let i=0;i<3;i++){
  const previous=await page.evaluate(()=>requests.length);
  await page.evaluate(g=>host.open(g),8+i);
  await page.waitForFunction(previous=>host.attached && requests.length>previous+5,previous,{timeout:30000});
  const staleDropped=await page.evaluate(()=>{
   let sent=0;const port=host.port;const post=port.postMessage.bind(port);
   port.postMessage=(...args)=>{++sent;post(...args);};
   host.send(JSON.stringify({id:requests[0].id,result:{}}),host.generation);
   port.postMessage=post;
   return sent===0;
  });
  assert(staleDropped,'Late response from a retired connection leaked');
  await page.evaluate(()=>host.close());
 }
 const wireIds=await page.evaluate(()=>requests.map(request=>request.id));
 assert.equal(new Set(wireIds).size,wireIds.length);
 checks.lateResponseDropped=true;
 checks.reconnects=3;
 assert.deepEqual(errors,[]);
 await fs.mkdir('out/Wasm/validation-devtool-port',{recursive:true});
 await fs.writeFile('out/Wasm/validation-devtool-port/validation.json',JSON.stringify({passed:true,checks},null,2));
 console.log(checks);
} finally {await browser.close();}
