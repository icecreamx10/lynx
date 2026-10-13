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
const browser=await (isWebKit?webkit:chromium).launch(isWebKit?{headless:true}:{executablePath:process.env.CHROME_PATH,headless:true,args:['--enable-unsafe-webgpu']});
const page=await browser.newPage();
const source=await fs.readFile('platform/wasm/app/src/image-fixtures.ts','utf8');
const fixtures=JSON.parse(source.slice(source.indexOf('=')+1).trim().replace(/;$/,''));
const checks={};
try {
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6096/');
 await page.waitForFunction(()=>typeof createLynxBitmapAnimation==='function',null,{timeout:60000});
 const oracle=isWebKit?JSON.parse(await fs.readFile('out/Wasm/validation-bitmap-animation/validation.json','utf8')).checks:null;
 for(const name of ['gif','animatedWebp','gifPatches','webpPatches']) {
  const result=await page.evaluate(async ({uri,expected})=>{
   const blob=await (await fetch(uri)).blob(),bytes=new Uint8Array(await blob.arrayBuffer());
   const fallback=createLynxBitmapAnimation(bytes,blob.type,8192);
   const reference=typeof ImageDecoder!=='undefined'?new ImageDecoder({data:bytes,type:blob.type,preferAnimation:true}):null;
   if(reference)await reference.tracks.ready;
   const count=reference?reference.tracks.selectedTrack.frameCount:fallback.tracks.selectedTrack.frameCount;
   const pixels=frame=>{
     const c=document.createElement('canvas');c.width=frame.displayWidth||frame.width;c.height=frame.displayHeight||frame.height;
     const ctx=c.getContext('2d');ctx.drawImage(frame,0,0);return [...ctx.getImageData(0,0,c.width,c.height).data];
   };
   const differences=[],durations=[],pixelHashes=[];
   const hash=p=>p.reduce((h,b)=>Math.imul(h^b,16777619)>>>0,2166136261);
   try {
     for(let loop=0;loop<2;loop++)for(let index=0;index<count;index++) {
       const a=await fallback.decode({frameIndex:index}),b=reference?await reference.decode({frameIndex:index}):null;
       const pa=pixels(a.image),pb=b?pixels(b.image):pa;
       const signature=hash(pa);pixelHashes.push(signature);
       if(expected && signature!==expected.pixelHashes[loop*count+index])throw new Error('WebKit frame differs from Chrome oracle');
       let max=0,different=0;for(let i=0;i<pa.length;i++){const delta=Math.abs(pa[i]-pb[i]);if(delta>max)max=delta;if(delta>2)different++;}
       differences.push({loop,index,max,count:different});durations.push([a.duration,b?b.image.duration:expected.durations[loop*count+index][0]]);
       a.image.close();if(b)b.image.close();
       // Repeating a decode after pausing must not apply disposal/blending twice.
       const repeated=await fallback.decode({frameIndex:index});
       if(JSON.stringify(pixels(repeated.image))!==JSON.stringify(pa))throw new Error('Repeated frame differs');
       repeated.image.close();
     }
     let malformedRejected=false;
     try{createLynxBitmapAnimation(bytes.subarray(0,20),blob.type,8192);}catch{malformedRejected=true;}
     return {count,differences,durations,pixelHashes,malformedRejected};
   } finally {fallback.close();if(reference)reference.close();}
  },{uri:fixtures[name],expected:oracle?.[name]});
  assert(result.malformedRejected);assert(result.differences.every(d=>d.count===0),JSON.stringify({name,...result}));
  assert(result.durations.every(([a,b])=>a===b));checks[name]={passed:true,...result};console.log('PASS:',name);
 }
 const output=path.resolve(isWebKit?'out/Wasm/validation-bitmap-animation-webkit':'out/Wasm/validation-bitmap-animation');await fs.mkdir(output,{recursive:true});
 await fs.writeFile(path.join(output,'validation.json'),JSON.stringify({passed:true,browser:isWebKit?'WebKit':'Chrome',checks},null,2));
} finally {await browser.close();}
