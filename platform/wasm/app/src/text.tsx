// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import { root, useState } from '@lynx-js/react';
import './text.css';
const common = { fontSize: '20px', color: '#eaf2ff', lineHeight: '28px' };
function App() {
  const [changed, setChanged] = useState(false);
  const [layouts, setLayouts] = useState(0);
  const [unicodeEnd, setUnicodeEnd] = useState(-1);
  return <view style={{ width: '800px', height: '500px', padding: '20px', backgroundColor: '#101827' }}>
    <text id="title" style={{ ...common, fontSize: '26px', height: '38px' }}>TTText · 中文 / HarfBuzz / Skity WebGPU</text>
    <view style={{ display: 'flex', flexDirection: 'row', width: '760px' }}>
      <view style={{ width: '360px', marginRight: '30px' }}>
        <text id="wrap" style={{ ...common, width: '240px' }}>中文自动换行：真实 Lynx 页面使用 TTText 排版，测量和绘制共享文字坐标。</text>
        <text id="newline" style={{ ...common, marginTop: '12px', width: '350px' }}>{'显式换行 First line\n第二行 Second line'}</text>
        <text id="rich" style={{ ...common, marginTop: '12px', width: '350px' }}>富文本 <text style={{ color: '#ff5555', fontWeight: '700', fontSize: '26px' }}>红色粗体</text> <text className="green-underline">绿色下划线</text></text>
        <text id="fallback" bindlayout={event => setUnicodeEnd(event.detail.lines[event.detail.lines.length - 1]?.end ?? -1)} style={{ ...common, marginTop: '12px', width: '350px' }}>Latin 中文 😀 🚀 é</text>
        <text id="arabic" style={{ ...common, marginTop: '12px', width: '350px' }}>Arabic: مرحبا بالعالم</text>
        <text id="nowrap" style={{ ...common, width: '200px', marginTop: '12px', whiteSpace: 'nowrap', textOverflow: 'clip' }}>No wrap clip: ABCDEFGHIJKLMNOPQRSTUVWXYZ</text>
      </view>
      <view style={{ width: '370px' }}>
        <text id="center" style={{ ...common, width: '350px', height: '40px', textAlign: 'center', backgroundColor: '#19334a' }}>居中 Center</text>
        <text id="right" style={{ ...common, width: '350px', height: '40px', marginTop: '12px', textAlign: 'right', backgroundColor: '#19334a' }}>右对齐 Right</text>
        <text id="ellipsis" text-maxline="2" style={{ ...common, width: '220px', marginTop: '12px', textOverflow: 'ellipsis' }}>两行省略：这段文字足够长，需要在第二行末尾显示省略号，不能覆盖下方内容。</text>
        <text id="longword" style={{ ...common, width: '220px', marginTop: '12px' }}>LongWord0123456789012345678901234567890123456789</text>
        <text id="dynamic" bindlayout={() => setLayouts(n => n + 1)} style={{ ...common, width: changed ? '180px' : '350px', marginTop: '12px', color: changed ? '#ffcc44' : '#eaf2ff' }}>{changed ? '更新后的中文内容变长，宽度变小，需要重新排版。' : '点击按钮更新文字'}</text>
        <text id="after" style={{ fontSize: '14px', color: '#89a7c2' }}>Following text moves with layout</text>
      </view>
    </view>
    <view id="toggle" bindtap={() => setChanged(v => !v)} style={{ position: 'absolute', left: '20px', top: '440px', width: '210px', height: '40px', backgroundColor: '#315373', padding: '6px' }}><text style={common}>更新文字 / Toggle</text></view>
    <text id="events" style={{ position: 'absolute', left: '260px', top: '445px', fontSize: '16px', color: '#89a7c2' }}>{`layout events: ${layouts} UTF16 end: ${unicodeEnd}`}</text>
  </view>;
}
root.render(<App />);
