// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

// Executed inside PrimJS/QuickJS in WASM, not by browser JavaScript.
// This probe uses a small scene adapter, not a production Lynx bundle loader.
JSON.stringify({
  width: 800, height: 500, padding: 32, gap: 20,
  background: '#101827', direction: 'column',
  children: [
    { height: 64, background: '#19263d', text: 'Lynx + QuickJS / WASM', size: 26 },
    { height: 40, text: 'Starlight layout -> Skity WebGPU -> canvas', size: 18 },
    { height: 152, direction: 'row', gap: 16, children:
      ['QuickJS', 'Lynx layout', 'WebGPU'].map((text, index) => ({
        width: 234, height: 152,
        background: ['#174a48', '#283d67', '#503663'][index],
        text, size: 24,
      }))
    },
    { height: 64, background: '#19263d',
      text: 'Static canvas rendered from C++ / WASM', size: 18 },
  ],
})
