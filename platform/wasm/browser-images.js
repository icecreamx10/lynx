// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
// Browser resource/decode adapter. Pixels stay outside the WASM heap.
globalThis.createLynxBrowserImages = function(module, device) {
  const records = new Map();
  const stats = {requests:0, cancelled:0, uploads:0, errors:0, closedFrames:0};
  const live = r => records.get(r.token) === r;
  const call = (name, types, args) => module.ccall(name, 'number', types, args);
  const event = (r, name) => { if (live(r)) call('image_playback_event', ['number','string'], [r.token,name]); };
  function format(bytes) {
    const ascii = (i,n) => String.fromCharCode(...bytes.subarray(i,i+n));
    if (bytes[0]===255 && bytes[1]===216 && bytes[2]===255) return {type:'image/jpeg',animated:false};
    if (ascii(0,6)==='GIF87a' || ascii(0,6)==='GIF89a') return {type:'image/gif',animated:true};
    const view = new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
    if (ascii(0,4)==='RIFF' && ascii(8,4)==='WEBP') {
      let animated=false;
      for(let p=12;p+8<=bytes.length;) {
        const n=view.getUint32(p+4,true);
        if(ascii(p,4)==='ANIM') animated=true;
        p+=8+n+(n%2);
      }
      return {type:'image/webp',animated};
    }
    if (ascii(1,3)==='PNG' && bytes[0]===137) {
      let animated=false;
      for(let p=8;p+12<=bytes.length;) {
        const n=view.getUint32(p,false);
        if(ascii(p+4,4)==='acTL') animated=true;
        p+=12+n;
      }
      return {type:'image/png',animated};
    }
    throw new Error('Supported image formats: PNG, JPEG, WebP and GIF');
  }
  function fail(r, error) {
    if (!live(r)) return;
    r.failed=true; r.playing=false;
    if(r.decoder) { r.decoder.close(); r.decoder=null; }
    stats.errors++;
    call('image_complete',['number','string'],[r.token,String(error.message || error)]);
  }
  function upload(r, frame) {
    const width=frame.displayWidth || frame.width, height=frame.displayHeight || frame.height;
    if(!live(r)) return false;
    if(width>device.limits.maxTextureDimension2D || height>device.limits.maxTextureDimension2D ||
       width*height>16*1024*1024) throw new Error('Image exceeds texture size limit');
    const handle=call('image_allocate',['number','number','number'],[r.token,width,height]);
    if(!handle) throw new Error('Could not allocate image texture');
    const texture=module.WebGPU.getJsObject(handle);
    // Skity samples premultiplied RGBA. Queue ordering protects previously
    // submitted draws when the same animation texture is updated.
    device.queue.copyExternalImageToTexture({source:frame},
      {texture,premultipliedAlpha:true,colorSpace:'srgb'},[width,height]);
    stats.uploads++; r.uploads++;
    call('image_complete',['number','string'],[r.token,'']);
    return true;
  }
  async function decodeFrame(r,index,initial=false) {
    r.decoding=true;
    let frame;
    try {
      const result=await r.decoder.decode({frameIndex:index,completeFramesOnly:true});
      frame=result.image;
      if(!live(r) || (!initial && !r.autoplay)) return;
      if(upload(r,frame)) {
        r.index=index;
        r.duration=Math.max(10,(frame.duration || 100000)/1000);
        r.remaining=r.duration;
        r.lastTime=performance.now();
      }
    } finally {
      if(frame) { frame.close(); stats.closedFrames++; }
      r.decoding=false;
    }
  }
  async function load(r) {
    let bitmap;
    try {
      const url=new URL(r.src, document.baseURI);
      if(!['http:','https:','data:','blob:'].includes(url.protocol)) throw new Error('Unsupported image URL protocol');
      const response=await fetch(url,{signal:r.abort.signal});
      if(!response.ok) throw new Error(`Image HTTP ${response.status}`);
      const blob=await response.blob();
      if(blob.size>32*1024*1024) throw new Error('Encoded image exceeds 32 MiB');
      const bytes=new Uint8Array(await blob.arrayBuffer());
      if(!live(r)) return;
      const f=format(bytes); r.type=f.type; r.animated=f.animated;
      if(f.animated) {
        if(typeof ImageDecoder==='undefined' || !await ImageDecoder.isTypeSupported(f.type))
          throw new Error('This browser cannot decode animated '+f.type);
        if(!live(r)) return;
        r.decoder=new ImageDecoder({data:bytes,type:f.type,preferAnimation:true});
        await r.decoder.tracks.ready;
        if(!live(r)) return;
        r.count=r.decoder.tracks.selectedTrack.frameCount;
        if(!r.count) throw new Error('Animated image has no frames');
        await decodeFrame(r,0,true);
        r.ready=true;
      } else {
        bitmap=await createImageBitmap(new Blob([bytes],{type:f.type}),{premultiplyAlpha:'premultiply'});
        if(!live(r)) return;
        upload(r,bitmap); r.ready=true;
      }
    } catch(error) { if(live(r)) fail(r,error); }
    finally { if(bitmap) {bitmap.close(); stats.closedFrames++;} }
  }
  function cancel(token) {
    const r=records.get(token); if(!r) return;
    records.delete(token); r.abort.abort();
    if(r.decoder) {r.decoder.close(); r.decoder=null;}
    stats.cancelled++;
  }
  return {
    stats,
    request(token,src,autoplay,loops) {
      const r={token,src,autoplay,limit:loops,abort:new AbortController(),
        index:0,count:1,loops:0,uploads:0,ready:false,playing:false,ended:false,
        failed:false,decoding:false,lastTime:null,duration:100,remaining:100};
      records.set(token,r); stats.requests++; void load(r);
    },
    cancel,
    configure(token,autoplay,loops) {
      const r=records.get(token); if(!r) return;
      if(r.limit!==loops && (loops===0 || r.loops<loops)) r.ended=false;
      r.autoplay=autoplay; r.limit=loops; r.lastTime=null;
    },
    tick(time) {
      for(const r of records.values()) {
        if(!r.ready || !r.animated || r.count<=1 || r.failed || r.ended || !r.autoplay || r.decoding) {
          r.lastTime=null; continue;
        }
        if(!r.playing) {r.playing=true; event(r,'startplay');}
        if(r.lastTime===null) {r.lastTime=time;continue;}
        const elapsed=time-r.lastTime; r.lastTime=time;
        // Visibility changes reset lastTime in suspend(). A visible but slow
        // rAF must still advance: dropping intervals >250 ms freezes every
        // image when the host stays below 4 FPS. Clamp catch-up instead.
        if(elapsed<0) continue;
        r.remaining-=Math.min(elapsed,250);
        if(r.remaining>0) continue;
        let next=r.index+1;
        if(next>=r.count) {
          r.loops++; event(r,'currentloopcomplete');
          if(r.limit>0 && r.loops>=r.limit) {
            r.ended=true; r.playing=false; event(r,'finalloopcomplete'); continue;
          }
          next=0;
        }
        void decodeFrame(r,next).catch(error=>fail(r,error));
      }
    },
    suspend() { for(const r of records.values()) r.lastTime=null; },
    inspect() { return [...records.values()].map(r=>({token:r.token,type:r.type,
      animated:r.animated,index:r.index,count:r.count,loops:r.loops,uploads:r.uploads,
      autoplay:r.autoplay,ended:r.ended,ready:r.ready,failed:r.failed,decoding:r.decoding})); }
  };
};
