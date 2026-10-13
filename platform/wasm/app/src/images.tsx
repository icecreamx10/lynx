// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import {root,useState} from '@lynx-js/react';
import {images} from './image-fixtures';
const position=(x,y,w,h)=>({position:'absolute',left:`${x}px`,top:`${y}px`,width:`${w}px`,height:`${h}px`});
function App() {
  const [loaded,setLoaded]=useState({});
  const [loadCalls,setLoadCalls]=useState(0);
  const [errors,setErrors]=useState(0);
  const [loops,setLoops]=useState(0);
  const [finals,setFinals]=useState(0);
  const [starts,setStarts]=useState(0);
  const [playing,setPlaying]=useState(false);
  const [source,setSource]=useState(images.png);
  const [visible,setVisible]=useState(true);
  function onAlphaLoad(event) {
    'main thread';
    event.currentTarget.setStyleProperty('background-color',
      event.detail.width===40 && event.detail.height===40 ? '#505050' : '#ff00ff');
  }
  const onLoad=name=>e=>{setLoadCalls(n=>n+1);setLoaded(v=>({...v,[name]:`${e.detail.width}x${e.detail.height}`}));};
  const onError=e=>setErrors(n=>typeof e.detail.errMsg==='string' && e.detail.error_code===-1?n+1:-100);
  const text=(value,x,y,w=220)=><text style={{...position(x,y,w,25),fontSize:'14px',color:'#eaf2ff'}}>{value}</text>;
  return <view style={{position:'relative',width:'800px',height:'500px',backgroundColor:'#121a28'}}>
    {text('Lynx images / browser decode / Skity WebGPU',20,12,740)}
    {['png','jpeg','webp'].flatMap((name,f)=>['aspectFit','aspectFill'].map((mode,m)=>{
      const key=name+'-'+mode,x=20+(f*2+m)*125;
      return <view key={key}>
        {text(name+' '+(m?'fill':'fit'),x,42,110)}
        <image src={images[name]} mode={mode} style={{...position(x,68,100,100),backgroundColor:'#505050'}} bindload={onLoad(key)} binderror={onError}/>
        {text(loaded[key] || 'Loading',x,174,110)}
      </view>;
    }))}
    <image src={images.gif} style={position(20,230,70,70)} bindload={onLoad('gif')} binderror={onError} bindstartplay={()=>setStarts(n=>n+1)}/>
    {text('GIF infinite',20,204,110)}
    <image src={images.animatedWebp} loop-count={2} style={position(145,230,70,70)} bindload={onLoad('anim-webp')} binderror={onError} bindcurrentloopcomplete={()=>setLoops(n=>n+1)} bindfinalloopcomplete={()=>setFinals(n=>n+1)}/>
    {text('WebP x2',145,204,110)}
    <image src={images.gif} autoplay={playing} style={position(270,230,70,70)} bindload={onLoad('paused-gif')} binderror={onError}/>
    <view style={{...position(270,310,100,30),backgroundColor:'#345575'}} bindtap={()=>setPlaying(v=>!v)}>{text(playing?'Pause GIF':'Play GIF',4,3,96)}</view>
    <image src={images.alpha} style={{...position(395,230,70,70),backgroundColor:'#000000'}} bindload={onLoad('alpha')} main-thread:bindload={onAlphaLoad} binderror={onError}/>
    {text('PNG alpha',395,204,110)}
    <image src={images.portrait} mode="aspectFit" style={{...position(520,230,100,100),backgroundColor:'#505050'}} bindload={onLoad('portrait-fit')} binderror={onError}/>
    <image src={images.portrait} mode="aspectFill" style={{...position(645,230,100,100),backgroundColor:'#505050'}} bindload={onLoad('portrait-fill')} binderror={onError}/>
    {text('Portrait fit',520,204,110)}{text('Portrait fill',645,204,110)}
    {visible && <image src={source} mode="aspectFill" style={position(20,385,80,60)} bindload={onLoad('replace')} binderror={onError}/>}
    <view style={{...position(120,390,120,35),backgroundColor:'#345575'}} bindtap={()=>setSource(v=>v===images.png?images.webp:images.png)}>{text('Replace src',4,6,116)}</view>
    <view style={{...position(255,390,120,35),backgroundColor:'#345575'}} bindtap={()=>setVisible(v=>!v)}>{text(visible?'Remove image':'Add image',4,6,116)}</view>
    <image src={images.broken} style={position(395,385,50,50)} binderror={onError}/>
    {text('Broken image',455,392,160)}
    {text(`Loads: ${Object.keys(loaded).length} Errors: ${errors} Loops: ${loops} Finals: ${finals} Starts: ${starts} Calls: ${loadCalls}`,20,464,760)}
  </view>;
}
root.render(<App/>);
