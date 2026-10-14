// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
globalThis.document = {baseURI:'https://host.example/preview/'};
vm.runInThisContext(await fs.readFile('platform/wasm/browser-resources.js', 'utf8'));
const create = globalThis.createLynxWebResourceProvider;
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
let context;
const p = create({provider:async request => {
  context = request;
  return {data:new Uint8Array([1,2,3]), mime:'image/png'};
}});
assert.deepEqual([...((await p.load({url:'image.png',type:'image'}).promise).data)], [1,2,3]);
assert.equal(context.url, 'https://host.example/preview/image.png');
assert.equal(context.type, 'image');
assert.equal(p.inspect()[0].bytes, 3);
// Custom schemes and binary sources never need a socket or fetch server.
assert.equal((await p.load({url:'devtools://session/font',type:'font'}).promise).mime, 'image/png');
await assert.rejects(p.load({url:'big',maxBytes:2}).promise, /size limit/);
await assert.rejects(create({provider:()=>new Response('',{status:404})}).load({url:'404'}).promise, /HTTP 404/);
await assert.rejects(create({provider:()=>new Response(new Uint8Array(4))}).load({url:'large',maxBytes:3}).promise, /size limit/);
await assert.rejects(create({provider:()=>({data:'not binary'})}).load({url:'bad'}).promise, /binary data/);
// A provider that ignores cancellation cannot publish a late result.
const slow = create({provider:async()=>{await delay(70);return new Uint8Array([9]);}});
const handle = slow.load({url:'slow'});handle.cancel();
await assert.rejects(handle.promise, e => e.name==='AbortError');
await delay(90);assert.equal(slow.inspect()[0].state,'cancelled');
const controller = new AbortController();controller.abort();
await assert.rejects(p.load({url:'pre-aborted',signal:controller.signal}).promise,e=>e.name==='AbortError');
const timeout = create({timeout:10,provider:()=>new Promise(()=>{})});
await assert.rejects(timeout.load({url:'timeout'}).promise,/timed out/);
assert.equal(timeout.inspect()[0].state,'error');
const stream = create({provider:()=>new Response(new ReadableStream({start(c){c.enqueue(new Uint8Array(4));c.close();}}))});
await assert.rejects(stream.load({url:'stream',maxBytes:3}).promise,/size limit/);
for(let i=0;i<150;i++) await p.load({url:`bounded-${i}`}).promise;
assert.equal(p.inspect().length,128);
console.log(JSON.stringify({passed:true,checks:['relative URL','typed requests','custom schemes','binary bytes','HTTP errors','size limits','stream limits','cancellation','late completion','pre-abort','timeout','bounded diagnostics']}));
