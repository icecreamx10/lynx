// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import { root } from '@lynx-js/react';

const OBJECT_REPLACEMENT = '\uFFFC';
const INITIAL_RUNS = [
  '# EditContext foundation\n',
  'Select across ',
  ' and ',
  'keep typing',
  '.\n```js const answer = 42;```\n',
  '\nContinue editing after the block atom.',
];
const INITIAL_PROJECTION =
  INITIAL_RUNS[0] +
  INITIAL_RUNS[1] +
  OBJECT_REPLACEMENT +
  INITIAL_RUNS[2] +
  INITIAL_RUNS[3] +
  INITIAL_RUNS[4] +
  OBJECT_REPLACEMENT +
  INITIAL_RUNS[5];

function App() {
  return (
    <scroll-view
      scroll-orientation="vertical"
      style={{ width: '100%', height: '100%', backgroundColor: '#f2f4f8' }}
    >
      <view style={{ padding: '28px' }}>
        <text style={{ color: '#5b67f1', fontSize: '13px', fontWeight: '700' }}>
          W3C EDITCONTEXT FOUNDATION
        </text>
        <text
          style={{
            color: '#151825',
            fontSize: '30px',
            fontWeight: '700',
            marginTop: '8px',
          }}
        >
          Frontend-owned Markdown editor
        </text>
        <text style={{ color: '#646b7a', fontSize: '15px', marginTop: '8px' }}>
          One UTF-16 projection; blocks, inline atoms, marks and history stay in
          JavaScript.
        </text>

        <view
          style={{
            marginTop: '20px',
            padding: '20px',
            borderRadius: '12px',
            backgroundColor: '#ffffff',
          }}
        >
          <text
            id="edit-context-status"
            style={{ color: '#16825d', fontSize: '14px', marginTop: '16px' }}
          >
            RENDER BASELINE — EditContext is completely disabled
          </text>

          <text
            id="editing-host"
            style={{
              marginTop: '16px',
              minHeight: '260px',
              padding: '18px',
              border: '2px solid #c8cdfd',
              backgroundColor: '#fbfbfe',
              color: '#242735',
              fontSize: '18px',
              lineHeight: '32px',
              whiteSpace: 'pre-wrap',
            }}
          >
            <text id="run-heading">{INITIAL_RUNS[0]}</text>
            <text id="run-prefix">{INITIAL_RUNS[1]}</text>
            <view
              {...({ contenteditable: 'false' } as any)}
              style={{
                display: 'inline-block',
                padding: '3px 8px',
                backgroundColor: '#e6e8ff',
              }}
            >
              <text style={{ color: '#3842a4', fontSize: '14px' }}>@Ada</text>
            </view>
            <text id="run-middle">{INITIAL_RUNS[2]}</text>
            <text
              id="run-marked"
              style={{ backgroundColor: '#fff0bd' }}
            >
              {INITIAL_RUNS[3]}
            </text>
            <text
              id="run-code"
              style={{ color: '#d7e2ff', backgroundColor: '#20263a' }}
            >
              {INITIAL_RUNS[4]}
            </text>
            <view
              {...({ contenteditable: 'false' } as any)}
              style={{
                display: 'block',
                width: '100%',
                padding: '12px',
                backgroundColor: '#e8f8f1',
              }}
            >
              <text style={{ color: '#187253' }}>▣ diagram</text>
            </view>
            <text id="run-tail">{INITIAL_RUNS[5]}</text>
          </text>
        </view>
      </view>
    </scroll-view>
  );
}

root.render(<App />);
