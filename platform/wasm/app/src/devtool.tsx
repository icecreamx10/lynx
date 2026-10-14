// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import { root, useEffect, useState } from '@lynx-js/react';
import './style.css';

// A real bundle fixture for the existing Inspector's source/locals tests.
function inspectableIncrement(value: number) {
  const next = value + 1;
  console.log('Lynx Inspector counter', next);
  return next;
}
function App() {
  const [count, setCount] = useState(0);
  useEffect(() => {
    (globalThis as any).__lynxInspectorIncrement = inspectableIncrement;
    console.log('Lynx Inspector fixture ready');
  }, []);
  return <view className="page">
    <view className="header"><text className="title">Lynx DevTool in WASM</text></view>
    <text className="subtitle">Official frontend / existing native Inspector</text>
    <view className="cards">
      <view id="counter" className="card blue" bindtap={() => setCount(inspectableIncrement)}>
        <text className="label">{`Count: ${count}`}</text>
      </view>
    </view>
  </view>;
}
root.render(<App />);
