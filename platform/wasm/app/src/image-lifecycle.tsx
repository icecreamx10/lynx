// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
// Network fixtures are fulfilled locally by verify-images.mjs, never deployed.
import {root,useState} from '@lynx-js/react';
import {images} from './image-fixtures';
function App() {
 const [src,setSrc]=useState('/fixtures/slow.png');
 const [visible,setVisible]=useState(true);
 const [loads,setLoads]=useState(0),[errors,setErrors]=useState(0),[stale,setStale]=useState(0);
 const box=(x,y,w=100,h=100)=>({position:'absolute',left:`${x}px`,top:`${y}px`,width:`${w}px`,height:`${h}px`});
 const error=e=>setErrors(n=>typeof e.detail.errMsg==='string'&&e.detail.error_code===-1?n+1:-100);
 const label=(t,x,y,w=300)=><text style={{...box(x,y,w,30),color:'#eaf2ff',fontSize:'16px'}}>{t}</text>;
 return <view style={{width:'800px',height:'500px',position:'relative',backgroundColor:'#121a28'}}>
  {visible && <image src={src} style={box(20,20)} bindload={e=>{setLoads(n=>n+1);if(e.detail.width===201)setStale(n=>n+1);}} binderror={error}/>}
  <image src="/fixtures/not-found.png" style={box(145,20)} binderror={error}/>
  <image src="http://127.0.0.1:6196/no-cors.png" style={box(270,20)} binderror={error}/>
  <image src="/fixtures/bad.png" style={box(395,20)} binderror={error}/>
  <image src="http://127.0.0.1:6196/allowed.png" style={box(520,20)} bindload={()=>setLoads(n=>n+1)} binderror={error}/>
  <image src="/fixtures/animated.gif" loop-count={1} style={box(645,20)} bindload={()=>setLoads(n=>n+1)} binderror={error}/>
  <view style={{...box(20,180,160,40),backgroundColor:'#345575'}} bindtap={()=>setSrc(images.webp)}>{label('Replace slow src',5,5,155)}</view>
  <view style={{...box(200,180,160,40),backgroundColor:'#345575'}} bindtap={()=>setVisible(v=>!v)}>{label('Remove slow node',5,5,155)}</view>
  {label(`Loads: ${loads} Errors: ${errors} Stale: ${stale}`,20,300,750)}
 </view>;
}
root.render(<App/>);
