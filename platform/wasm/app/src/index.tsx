// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import { root } from '@lynx-js/react';
import './style.css';
function App() {
  return <view className="page">
    <view className="header"><text className="title">A real Lynx page</text></view>
    <text className="subtitle">ReactLynx bundle running in WASM</text>
    <view className="cards">
      <view className="card green"><text className="label">Bundle decoded</text></view>
      <view className="card blue"><text className="label">QJS executed</text></view>
      <view className="card purple"><text className="label">WebGPU canvas</text></view>
    </view>
    <text className="footer">JSX + CSS -> Lynx bundle -> native renderer</text>
  </view>;
}
root.render(<App />);
