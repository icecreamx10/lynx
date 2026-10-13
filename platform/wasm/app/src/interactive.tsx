// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import { root, useState, useEffect, useMainThreadRef } from '@lynx-js/react';
import './style.css';
function App() {
  const toggle = useMainThreadRef(false);
  const [count, setCount] = useState(0);
  const [running, setRunning] = useState(false);
  const [frame, setFrame] = useState(0);
  useEffect(() => {
    if (!running) return;
    let alive = true;
    let token = 0;
    const step = () => {
      if (!alive) return;
      setFrame(n => n + 1);
      token = requestAnimationFrame(step);
    };
    token = requestAnimationFrame(step);
    return () => { alive = false; cancelAnimationFrame(token); };
  }, [running]);
  function changeColor(event) {
    'main thread';
    toggle.current = !toggle.current;
    event.currentTarget.setStyleProperty('background-color', toggle.current ? '#23765b' : '#174a48');
  }
  return <view className="page">
    <view className="header"><text className="title">Interactive Lynx in WASM</text></view>
    <text className="subtitle">Real bundle / two QJS contexts / WebGPU</text>
    <view className="cards">
      <view className="card green" main-thread:bindtap={changeColor}><text className="label">Tap to change color</text></view>
      <view className="card blue" bindtap={() => setCount(n => n + 1)}><text className="label">{`Count: ${count}`}</text></view>
      <view className="card purple" bindtap={() => setRunning(v => !v)}>
        <text className="label">{running ? 'Stop animation' : 'Start animation'}</text>
        <view style={{width:'20px',height:'20px',marginLeft:`${16 + frame % 160}px`,marginTop:'24px',backgroundColor:'#eaf2ff'}} />
      </view>
    </view>
    <text className="footer">{`Animation frame: ${frame}`}</text>
  </view>;
}
root.render(<App />);
