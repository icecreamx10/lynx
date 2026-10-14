// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const out=process.env.PROBE_OUT || 'out/Wasm/validation-worker-resources';
await fs.mkdir(out,{recursive:true});
const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',args:['--enable-unsafe-webgpu']});
const page=await browser.newPage({viewport:{width:1600,height:1200}}),errors=[],checks={};
page.on('pageerror',e=>errors.push(String(e)));
const flat=n=>[n,...n.children.flatMap(flat)];
const read=()=>page.evaluate(()=>lynxCanvas.call('state'));
const load=async file=>{await page.locator('#bundle-file').setInputFiles(file);await page.locator('#load-bundle').click();await page.waitForFunction(()=>document.querySelector('#load-status').dataset.state==='success',null,{timeout:60000});};
const glyphPoints=n=>{const p=[];for(let y=Math.ceil(n.y);y<Math.min(500,n.y+n.height);y+=2)for(let x=Math.ceil(n.x);x<Math.min(800,n.x+n.width);x+=2)p.push([x,y]);return p;};
try {
 await page.addInitScript(()=>{window.providerRequests=[];window.fontGate=new Promise(resolve=>window.releaseFonts=resolve);window.lynxResourceProvider=async request=>{providerRequests.push({type:request.type,url:request.url});if(request.url.includes('NotoSansSC'))await fontGate;return fetch(request.url,{signal:request.signal});};});
 await page.goto(process.env.PROBE_URL || 'http://127.0.0.1:6150/');
 await page.waitForFunction(()=>window.probeResult?.backgroundRuntime,null,{timeout:60000});
 await page.getByRole('button',{name:'关闭 Lynx DevTool',exact:true}).waitFor();
 await page.locator('#toggle-devtool').click();
 const initial=await read();assert(initial.report.renderWorker);assert.equal(initial.fonts[0].state,'loaded');assert(initial.fonts.slice(1).every(f=>f.state==='idle'));
 checks.lazyStartup=true;
 await load(process.env.TEXT_BUNDLE || 'out/Wasm/text.lynx.bundle');
 const delayed=await read();assert.equal(delayed.fonts[1].state,'loading');assert(flat(delayed.report.tree).find(n=>n.id==='wrap').textLayout.missingGlyphs>0);
 await page.evaluate(()=>releaseFonts());
 for(let i=0;i<180;i++){const s=await read();if(s.fonts.every(f=>f.state==='loaded'))break;await page.waitForTimeout(500);}
 const state=await read();assert(state.fonts.every(f=>f.state==='loaded'));assert.deepEqual(state.report.errors,[]);
 const node=id=>{const n=flat(state.report.tree).find(n=>n.id===id);assert(n,id);return n;};
 for(const id of ['title','wrap','newline','rich','fallback','arabic','center','right','ellipsis','longword','dynamic','nowrap']){
  const l=node(id).textLayout;assert.equal(l.engine,'tttext-harfbuzz');assert(l.glyphs>0);assert.equal(l.missingGlyphs,0);
 }
 assert(node('wrap').textLayout.lines.length>=3);assert.equal(node('newline').textLayout.lines.length,2);
 assert(node('fallback').textLayout.fontCount>=3);assert(node('arabic').textLayout.contextualGlyphs>0);
 checks.text={externalFonts:true,chinese:true,arabic:true,emoji:true,missingGlyphs:0};
 const shot=await page.evaluate(points=>lynxCanvas.call('snapshot',[points]),glyphPoints(node('wrap')));
 assert(shot.pixels.filter(([r,g,b])=>r>150&&g>150&&b>150).length>40,'Chinese glyph pixels absent');
 checks.webgpuGlyphPixels=true;assert(state.report.frames>delayed.report.frames);checks.delayedFontReflow=true;
 const before=state.report.frames,box=await page.locator('#canvas').boundingBox();
 await page.mouse.click(box.x+1+100*box.width/802,box.y+1+460*box.height/502);
 await page.waitForFunction(()=>{const f=n=>[n,...n.children.flatMap(f)];return f(probeResult.tree).some(n=>n.id==='dynamic'&&n.text.startsWith('更新后的'));});
 assert((await read()).report.frames>before);checks.textStateReflow=true;
 const requests=await page.evaluate(()=>providerRequests);for(const family of ['Roboto-Regular','NotoSansSC','NotoSansArabic','NotoEmoji'])assert.equal(requests.filter(r=>r.url.includes(family)).length,1);
 await load(process.env.TEXT_BUNDLE || 'out/Wasm/text.lynx.bundle');assert.equal((await read()).report.btsThread,state.report.btsThread);assert.equal((await page.evaluate(()=>providerRequests)).length,requests.length);
 checks.customWindowProviderAndFontReuse=true;
 await page.setViewportSize({width:900,height:1300});const p=await page.locator('.preview-pane').boundingBox(),d=await page.locator('#devtool-controls').boundingBox();assert(d.y>=p.y+p.height);checks.narrowStacked=true;
 await page.screenshot({path:out+'/text.png',fullPage:true});
 assert.deepEqual(errors,[]);assert.deepEqual(await page.evaluate(()=>probeErrors),[]);
 await fs.writeFile(out+'/validation.json',JSON.stringify({passed:true,checks},null,2));console.log(JSON.stringify({passed:true,checks}));
} finally {await browser.close();}
