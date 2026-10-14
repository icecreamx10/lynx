// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import { defineConfig } from '@lynx-js/rspeedy';
import { pluginReactLynx } from '@lynx-js/react-rsbuild-plugin';
export default defineConfig({
  plugins: [pluginReactLynx({ debugInfoOutside: process.env.LYNX_CANVAS_DEVTOOL !== '1' })],
  dev: process.env.LYNX_CANVAS_DEVTOOL === '1'
    ? { hmr: false, liveReload: false, assetPrefix: '/' }
    : {},
  source: {
    entry: process.env.LYNX_CANVAS_DEVTOOL === '1'
      ? './src/devtool.tsx'
      : process.env.LYNX_CANVAS_TEXT === '1'
      ? './src/text.tsx'
      : process.env.LYNX_CANVAS_IMAGE_LIFECYCLE === '1'
      ? './src/image-lifecycle.tsx'
      : process.env.LYNX_CANVAS_IMAGES === '1'
        ? './src/images.tsx'
        : process.env.LYNX_CANVAS_INTERACTIVE === '1'
          ? './src/interactive.tsx'
          : './src/index.tsx',
  },
  output: { filename: 'main.lynx.bundle' },
});
