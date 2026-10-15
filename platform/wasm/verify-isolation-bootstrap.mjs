// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import {execFileSync} from 'node:child_process';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url),{chromium,webkit}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const bootstrap=await fs.readFile(new URL('./isolation-bootstrap.js',import.meta.url),'utf8');
const legacy=execFileSync('git',['show','72f93e86:platform/wasm/isolation-bootstrap.js'],{encoding:'utf8'});
const worker=(await fs.readFile(new URL('./isolation-worker.js',import.meta.url),'utf8'))
 .replace("self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));",'');
const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://localhost');
 res.setHeader('Cache-Control','no-store');
 if(url.pathname==='/isolation-worker.js') {res.setHeader('Content-Type','text/javascript');res.end(worker);}
 else if(url.pathname==='/isolation-bootstrap.js') {res.setHeader('Content-Type','text/javascript');res.end(url.searchParams.has('legacy')?legacy:bootstrap);}
 else {res.setHeader('Content-Type','text/html');res.end(`<script src="/isolation-bootstrap.js${url.searchParams.has('legacy')?'?legacy':''}"></script><script>window.lynxIsolationReady.then(()=>window.result='ready').catch(error=>window.result=error.message)</script>`);}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const base=`http://127.0.0.1:${server.address().port}/`,checks={};
try {
 for(const name of (process.env.PROBE_BROWSER?[process.env.PROBE_BROWSER]:['chromium','webkit'])) {
  const browser=await (name==='webkit'?webkit:chromium).launch(name==='webkit'?{headless:true}:{headless:true,executablePath:process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'});
  try {
   let context=await browser.newContext(),page=await context.newPage();
   await page.goto(base+'?legacy',{waitUntil:'commit'});
   await page.waitForFunction(()=>window.result,null,{timeout:20000});
   assert.equal(await page.evaluate(()=>result),'Isolation worker activation timed out');
   assert(await page.evaluate(async()=>!!(await navigator.serviceWorker.ready).active));
   assert.equal(await page.evaluate(()=>navigator.serviceWorker.controller),null);
   await context.close();
   context=await browser.newContext();page=await context.newPage();let navigations=0;
   page.on('framenavigated',frame=>{if(frame===page.mainFrame())++navigations;});
   await page.goto(base,{waitUntil:'commit'});
   await page.waitForFunction(()=>window.result,null,{timeout:20000});
   assert.equal(await page.evaluate(()=>result),'ready');assert.equal(navigations,2);
   assert(await page.evaluate(()=>crossOriginIsolated&&!!navigator.serviceWorker.controller));
   await page.reload();await page.waitForFunction(()=>window.result==='ready');assert.equal(navigations,3);
   checks[name]={legacyTimeoutReproduced:true,activeWithoutClaim:true,fixedReloads:1,isolated:true,subsequentLoadNoLoop:true};
   await context.close();
  } finally {await browser.close();}
 }
 const out=process.env.PROBE_OUT||'out/Wasm/validation-isolation-bootstrap';await fs.mkdir(out,{recursive:true});
 await fs.writeFile(out+'/validation.json',JSON.stringify({passed:true,checks},null,2));console.log(JSON.stringify({passed:true,checks}));
} finally {await new Promise(resolve=>server.close(resolve));}
