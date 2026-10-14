// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
// Parse animation containers only. The browser decodes each compressed image;
// Canvas2D composes frame patches, and the final page stays on Skity/WebGPU.
globalThis.createLynxBitmapAnimation = function(bytes, type, maxDimension) {
  const need=(ok,message='Malformed animated image')=>{if(!ok)throw new Error(message);};
  const u16=p=>{need(p+2<=bytes.length);return bytes[p]|bytes[p+1]<<8;};
  const u24=p=>{need(p+3<=bytes.length);return bytes[p]|bytes[p+1]<<8|bytes[p+2]<<16;};
  const u32=p=>{need(p+4<=bytes.length);return new DataView(bytes.buffer,bytes.byteOffset+p,4).getUint32(0,true);};
  const ascii=(p,n)=>String.fromCharCode(...bytes.subarray(p,p+n));
  const put16=(a,p,n)=>{a[p]=n&255;a[p+1]=n>>>8;};
  const put24=(a,p,n)=>{a[p]=n&255;a[p+1]=(n>>>8)&255;a[p+2]=n>>>16;};
  const text=s=>new TextEncoder().encode(s);
  const frames=[];
  let width=0,height=0,background=[0,0,0,0];
  const add=f=>{need(frames.length<4096,'Animation exceeds 4096 frames');frames.push(f);};
  if(type==='image/gif') {
    need(bytes.length>=13 && ['GIF87a','GIF89a'].includes(ascii(0,6)));
    width=u16(6);height=u16(8);
    const paletteSize=bytes[10]&128 ? 3*(1<<((bytes[10]&7)+1)):0;
    need(13+paletteSize<=bytes.length);
    const palette=bytes.subarray(13,13+paletteSize);
    const bg=bytes[11]*3;
    if(bg+3<=palette.length)background=[...palette.subarray(bg,bg+3),255];
    let p=13+paletteSize,gce=null;
    const blocks=()=>{for(;;){need(p<bytes.length);const n=bytes[p++];if(!n)return;need(p+n<=bytes.length);p+=n;}};
    while(p<bytes.length) {
      const marker=bytes[p++];
      if(marker===0x3b)break;
      if(marker===0x21) {
        need(p<bytes.length);const label=bytes[p++];
        if(label===0xf9) {
          need(p+6<=bytes.length && bytes[p]===4 && bytes[p+5]===0);
          const packed=bytes[p+1];gce={transparent:!!(packed&1),index:bytes[p+4],
            disposal:(packed>>>2)&7,duration:(u16(p+2)||10)*10};p+=6;
        } else {need(label!==0x01,'GIF plain-text graphics are unsupported');blocks();}
        continue;
      }
      need(marker===0x2c && p+9<=bytes.length);
      const x=u16(p),y=u16(p+2),w=u16(p+4),h=u16(p+6),flags=bytes[p+8];p+=9;
      const localSize=flags&128 ? 3*(1<<((flags&7)+1)):0;
      need(p+localSize<bytes.length);
      const local=bytes.subarray(p,p+localSize);p+=localSize;
      const dataStart=p;p++;blocks();
      const control=gce || {transparent:false,index:0,disposal:0,duration:100};gce=null;
      need(control.disposal<=3,'Unsupported GIF disposal method');
      const header=bytes.slice(0,13);put16(header,6,w);put16(header,8,h);
      const descriptor=new Uint8Array([0x2c,0,0,0,0,0,0,0,0,flags]);put16(descriptor,5,w);put16(descriptor,7,h);
      const singleGce=new Uint8Array([0x21,0xf9,4,control.transparent?1:0,0,0,control.index,0]);
      const blob=new Blob([header,palette,singleGce,descriptor,local,bytes.subarray(dataStart,p),new Uint8Array([0x3b])],{type});
      add({x,y,width:w,height:h,blob,duration:control.duration,disposal:control.disposal,
        clearTransparent:control.transparent,replace:false});
    }
    // Match browser treatment of transparent GIF canvas background.
    if(frames.some(f=>f.clearTransparent))background=[0,0,0,0];
  } else if(type==='image/webp') {
    need(bytes.length>=12 && ascii(0,4)==='RIFF' && ascii(8,4)==='WEBP');
    const end=u32(4)+8;need(end<=bytes.length);
    let icc=null,hasAnimation=false;
    const raw=[];
    for(let p=12;p+8<=end;) {
      const name=ascii(p,4),size=u32(p+4),start=p+8,next=start+size+(size%2);need(next<=end);
      if(name==='VP8X') {need(size===10);width=u24(start+4)+1;height=u24(start+7)+1;}
      if(name==='ICCP')icc=bytes.subarray(p,next);
      if(name==='ANIM') {need(size===6);hasAnimation=true;background=[bytes[start+2],bytes[start+1],bytes[start],bytes[start+3]];}
      if(name==='ANMF') {
        need(size>=16);const flags=bytes[start+15];need((flags&0xfc)===0);
        raw.push({x:u24(start)*2,y:u24(start+3)*2,width:u24(start+6)+1,height:u24(start+9)+1,
          duration:u24(start+12)||100,disposal:flags&1?2:0,replace:!!(flags&2),clearTransparent:false,
          start:start+16,end:start+size});
      }
      p=next;
    }
    need(hasAnimation && width && height);
    for(const f of raw) {
      const payload=[];let alpha=false,bitstreams=0;
      for(let p=f.start;p+8<=f.end;) {
        const name=ascii(p,4),size=u32(p+4),next=p+8+size+(size%2);need(next<=f.end);
        if(name==='ALPH'){alpha=true;payload.push(bytes.subarray(p,next));}
        if(name==='VP8 ' || name==='VP8L'){
          if(name==='VP8L' && size>=5)alpha ||= !!(u32(p+9)&0x10000000);
          bitstreams++;payload.push(bytes.subarray(p,next));
        }
        p=next;
      }
      need(bitstreams===1);
      const vp8x=new Uint8Array(18);vp8x.set(text('VP8X'));vp8x[4]=10;
      vp8x[8]=(alpha?0x10:0)|(icc?0x20:0);put24(vp8x,12,f.width-1);put24(vp8x,15,f.height-1);
      const parts=[vp8x,...(icc?[icc]:[]),...payload];
      const size=4+parts.reduce((n,a)=>n+a.length,0),header=new Uint8Array(12);
      header.set(text('RIFF'));new DataView(header.buffer).setUint32(4,size,true);header.set(text('WEBP'),8);
      add({...f,blob:new Blob([header,...parts],{type})});
    }
  } else throw new Error('Browser bitmap animation supports GIF and WebP');
  need(width>0 && height>0 && width<=maxDimension && height<=maxDimension && width*height<=16*1024*1024,'Animation exceeds texture size limit');
  need(frames.length>0,'Animated image has no frames');
  for(const f of frames)need(f.width>0 && f.height>0 && f.x+f.width<=width && f.y+f.height<=height);
  const canvas=(typeof document==='undefined' ? new OffscreenCanvas(width,height) : document.createElement('canvas'));canvas.width=width;canvas.height=height;
  const ctx=canvas.getContext('2d');need(ctx,'Browser cannot compose animated frames');
  let backup=null,last=-1,closed=false;
  const clear=(x,y,w,h,transparent=false)=>{
    ctx.clearRect(x,y,w,h);
    if(!transparent && background[3]){ctx.fillStyle=`rgba(${background[0]},${background[1]},${background[2]},${background[3]/255})`;ctx.fillRect(x,y,w,h);}
  };
  return {
    tracks:{ready:Promise.resolve(),selectedTrack:{frameCount:frames.length}},
    async decode({frameIndex:index}) {
      need(!closed,'Animation decoder is closed');need(index>=0 && index<frames.length);
      const f=frames[index];let patch=null;
      try {
        if(index!==last) {
          need(index===0 || index===last+1,'Animation frames must be decoded sequentially');
          patch=await createImageBitmap(f.blob,{premultiplyAlpha:'premultiply'});
          need(!closed,'Animation decoder is closed');need(patch.width===f.width && patch.height===f.height);
          if(index===0)clear(0,0,width,height);
          else {
            const previous=frames[last];
            if(previous.disposal===2)clear(previous.x,previous.y,previous.width,previous.height,previous.clearTransparent);
            if(previous.disposal===3 && backup){ctx.clearRect(0,0,width,height);ctx.drawImage(backup,0,0);}
          }
          if(f.disposal===3) {
            if(!backup){backup=(typeof document==='undefined' ? new OffscreenCanvas(width,height) : document.createElement('canvas'));backup.width=width;backup.height=height;}
            const saved=backup.getContext('2d');saved.clearRect(0,0,width,height);saved.drawImage(canvas,0,0);
          }
          if(f.replace)ctx.clearRect(f.x,f.y,f.width,f.height);
          ctx.drawImage(patch,f.x,f.y);last=index;
        }
        const image=await createImageBitmap(canvas);
        if(closed){image.close();throw new Error('Animation decoder is closed');}
        return {image,duration:f.duration*1000};
      } finally {if(patch)patch.close();}
    },
    close() {closed=true;canvas.width=0;canvas.height=0;if(backup){backup.width=0;backup.height=0;}frames.length=0;}
  };
};
