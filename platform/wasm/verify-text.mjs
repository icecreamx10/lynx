// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium,webkit}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const wk=process.env.PROBE_BROWSER==='webkit';
const browser=await (wk?webkit:chromium).launch(wk?{headless:true}:{headless:true,executablePath:process.env.CHROME_PATH,args:['--enable-unsafe-webgpu']});
const page=await browser.newPage({viewport:{width:1000,height:1200}});
const out=path.resolve(process.env.PROBE_OUTPUT || 'out/Wasm/validation-text');
await fs.mkdir(out,{recursive:true});
const errors=[];page.on('pageerror',e=>errors.push(String(e)));
const read=()=>page.evaluate(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])));
const flat=n=>[n,...n.children.flatMap(flat)];
const node=(r,id)=>{const n=flat(r.tree).find(n=>n.id===id);assert(n,`Missing ${id}`);return n;};
const wait=fn=>page.waitForFunction(fn,null,{timeout:90000});
const click=async(x,y)=>{const b=await page.locator('#canvas').boundingBox();await page.mouse.click(b.x+1+x,b.y+1+y);};
const snapshot=()=>page.evaluate(async()=>{
 window.pauseLynxFramePump=true;const m=window.lynxCanvas;
 await window.probeDevice.queue.onSubmittedWorkDone();m.ccall('release_completed_frames',null,[],[]);
 const id=m.ccall('render_frame','number',[],[]);
 const c=document.createElement('canvas');c.width=800;c.height=500;
 const ctx=c.getContext('2d');ctx.drawImage(document.querySelector('#canvas'),0,0);
 const data=[...ctx.getImageData(0,0,800,500).data];const png=c.toDataURL();
 await window.probeDevice.queue.onSubmittedWorkDone();m.ccall('release_frames',null,['number'],[id]);
 window.pauseLynxFramePump=false;return {data,png};
});
const pixels=(shot,n,predicate)=>{let count=0;for(let y=Math.max(0,Math.floor(n.y));y<Math.min(500,n.y+n.height);y++)for(let x=Math.max(0,Math.floor(n.x));x<Math.min(800,n.x+n.width);x++){const i=(y*800+x)*4;if(predicate(...shot.data.slice(i,i+4)))count++;}return count;};
const checks={};
try {
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6121/',{timeout:90000});
 await wait(()=>window.probeResult || window.probeErrors?.length);
 assert.deepEqual(await page.evaluate(()=>window.probeErrors),[]);
 await wait(()=>JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[])).backgroundRuntime);
 await wait(()=>window.lynxCanvas.lynxFonts.inspect().every(f=>f.state==='loaded'));
 await page.waitForTimeout(500);
 let r=await read();assert.deepEqual(r.errors,[]);assert.notEqual(r.mainThread,r.btsThread);
 for(const id of ['title','wrap','newline','rich','fallback','arabic','center','right','ellipsis','longword','dynamic','nowrap']){
  const n=node(r,id),l=n.textLayout;assert(l,`${id} missing TTText result`);assert.equal(l.engine,'tttext-harfbuzz');assert(l.glyphs>0,`${id} no drawn glyphs`);assert.equal(l.missingGlyphs,0,`${id} missing glyphs`);
  for(const line of l.lines){assert(line.width>=0);assert(line.height>0);assert(line.baseline>=line.y);assert(line.baseline<=line.y+line.height+1);}
 }
 const wrap=node(r,'wrap');assert(wrap.textLayout.lines.length>=3);assert(Math.abs(wrap.height-wrap.textLayout.height)<1);
 const newline=node(r,'newline');assert.equal(newline.textLayout.lines.length,2);assert.equal(newline.textLayout.lines[1].y-newline.textLayout.lines[0].y,28);
 checks.layout={passed:true,chineseLines:wrap.textLayout.lines.length,explicitLines:2,lineHeight:28,autoHeightMatchesPainting:true};
 const fallback=node(r,'fallback');assert(fallback.textLayout.fontCount>=3);assert.equal(fallback.textLayout.charCount,Array.from(fallback.text).length);
 const arabic=node(r,'arabic');assert(arabic.textLayout.fontCount>=2);assert(arabic.textLayout.contextualGlyphs>0,'Arabic did not produce contextual glyph substitutions');
 assert(node(r,'events').text.includes(`UTF16 end: ${fallback.text.length}`),'Layout event offsets are not UTF16');
 checks.unicode={passed:true,UTF16LayoutEvent:true,missingGlyphs:0,fallbackFonts:fallback.textLayout.fontCount,nonBMP:true,combiningMarks:true,arabicShaping:true};
 const center=node(r,'center'),right=node(r,'right');
 const cl=center.textLayout.lines[0],rl=right.textLayout.lines[0];
 assert(Math.abs(cl.x-(center.width-cl.width)/2)<1);assert(Math.abs(rl.x+rl.width-right.width)<1);
 checks.alignment={passed:true,center:true,right:true};
 const ellipsis=node(r,'ellipsis');assert.equal(ellipsis.textLayout.lines.length,2);assert(ellipsis.textLayout.exceededMaxLines);assert(Math.abs(ellipsis.height-56)<1);
 const word=node(r,'longword');assert(word.textLayout.lines.length>=2);assert(word.textLayout.lines.every(l=>l.width<=word.width+1));
 const nowrap=node(r,'nowrap');assert.equal(nowrap.textLayout.lines.length,1);assert(nowrap.textLayout.width>nowrap.width);assert.equal(nowrap.textLayout.charCount,nowrap.text.length);
 checks.overflow={passed:true,twoLineEllipsis:true,longWordWrap:true,nowrapClipsWholeLine:true};
 const rich=node(r,'rich');assert(rich.textLayout.runs.some(s=>s.color===0xffff5555&&s.size===26&&s.weight===700));assert(rich.textLayout.runs.some(s=>s.color===0xff55dd88&&s.size===20),'Nested text did not inherit parent font size');
 const shot=await snapshot();
 assert(pixels(shot,rich,(red,green,blue)=>red>180&&green<130&&blue<130)>50,'No red rich-text glyph pixels');
 assert(pixels(shot,rich,(red,green,blue)=>green>140&&red<130&&blue<180)>50,'No green rich-text glyph pixels');
 assert(pixels(shot,wrap,(red,green,blue)=>red>150&&green>150&&blue>150)>150,'No Chinese glyph pixels');
 await fs.writeFile(path.join(out,'canvas.png'),Buffer.from(shot.png.split(',')[1],'base64'));
 checks.paint={passed:true,skityWebGPU:true,chinesePixels:true,richColors:true,nestedWeightAndSize:true};
 const before=await read();const initialDynamic=node(before,'dynamic');const initialAfter=node(before,'after').y;
 await click(100,460);
 await wait(()=>{const flat=n=>[n,...n.children.flatMap(flat)];const r=JSON.parse(window.lynxCanvas.ccall('scene_report','string',[],[]));return flat(r.tree).some(n=>n.id==='dynamic'&&n.text.startsWith('更新后的'));});
 await page.waitForTimeout(300);r=await read();const changed=node(r,'dynamic');
 assert(r.frames>before.frames,'Text update did not invalidate canvas');assert(changed.textLayout.lines.length>initialDynamic.textLayout.lines.length);assert(changed.height>initialDynamic.height);assert(node(r,'after').y>initialAfter);
 assert.equal(changed.textLayout.runs[0].color,0xffffcc44);assert(parseInt(node(r,'events').text.split(': ')[1],10)>0);
 const updateShot=await snapshot();await fs.writeFile(path.join(out,'updated.png'),Buffer.from(updateShot.png.split(',')[1],'base64'));assert(pixels(updateShot,changed,(red,green,blue)=>red>180&&green>130&&blue<110)>100);
 await fs.writeFile(path.join(out,'updated.png'),Buffer.from(updateShot.png.split(',')[1],'base64'));
 const stableEvents=node(r,'events').text;await page.waitForTimeout(600);assert.equal(node(await read(),'events').text,stableEvents,'Layout events loop while idle');
 await click(100,460);await page.waitForTimeout(300);const restored=await read();assert.equal(node(restored,'dynamic').height,initialDynamic.height);assert.equal(node(restored,'after').y,initialAfter);
 checks.updates={passed:true,stateUpdates:true,autoHeightAndFollowingSibling:true,colorUpdate:true,layoutEvents:true,idleNoEventLoop:true};
 for(let i=0;i<3;i++) {await page.locator('#bundle-file').setInputFiles(path.resolve('out/Wasm/text.lynx.bundle'));await page.locator('#load-bundle').click();await wait(()=>document.querySelector('#load-status').dataset.state==='success');await page.waitForTimeout(150);assert.equal(node(await read(),'dynamic').text,'点击按钮更新文字');}
 r=await read();assert.deepEqual(r.errors,[]);assert.equal(r.btsThread,before.btsThread);assert.deepEqual(errors,[]);assert.deepEqual(await page.evaluate(()=>window.probeErrors),[]);
 checks.reload={passed:true,reloads:3,persistentBTS:true};
 await page.screenshot({path:path.join(out,'browser.png')});await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:true,browser:await browser.version(),checks,report:r},null,2));console.log(JSON.stringify({passed:true,checks},null,2));
}catch(error){await fs.writeFile(path.join(out,'validation.json'),JSON.stringify({passed:false,error:String(error),stack:error.stack,checks,report:await read().catch(()=>null)},null,2));throw error;}finally{await browser.close();}
