// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {gzipSync,brotliCompressSync,constants} from 'node:zlib';
import {createHash} from 'node:crypto';
const source=await fs.readFile('out/Wasm/text-resource-module.js','utf8');
let start=source.indexOf('return binaryDecode(');
assert(start>=0);start+= 'return binaryDecode('.length;
while(/\s/.test(source[start]))++start;
const quote=source[start];assert(quote==='"'||quote==="'");
let end=start+1;
while(end<source.length){if(source[end]==='\\'){end+=2;continue;}if(source[end]===quote)break;++end;}
const decoded=vm.runInNewContext(source.slice(start,end+1));
const wasm=Buffer.alloc(decoded.length);
for(let i=0;i<decoded.length;i++){const c=decoded.charCodeAt(i);wasm[i]=~c>>8&c;}
assert(WebAssembly.validate(wasm));
await fs.writeFile('out/Wasm/text-resource.extracted.wasm',wasm);
const sizes=data=>({bytes:data.length,gzip6:gzipSync(data,{level:6}).length,
 brotli6:brotliCompressSync(data,{params:{[constants.BROTLI_PARAM_QUALITY]:6}}).length,
 sha256:createHash('sha256').update(data).digest('hex')});
const fonts={};
for(const name of ['Roboto-Regular.ttf','NotoSansSC.ttf','NotoSansArabic.ttf','NotoEmoji.ttf']){
 const data=await fs.readFile('out/'+name);
 assert.equal(wasm.indexOf(data.subarray(0,512)),-1,`Font ${name} is embedded in WASM`);
 fonts[name]=sizes(data);
}
const report={fontDataEmbedded:false,wasm:sizes(wasm),html:sizes(await fs.readFile('out/Wasm/text-site/index.html')),fonts,
 note:'Compression sizes are measurements; the local preview server does not enable HTTP content encoding.'};
await fs.writeFile('out/Wasm/resource-sizes.json',JSON.stringify(report,null,2));
console.log(JSON.stringify(report,null,2));
