// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import { defineConfig } from '@lynx-js/rspeedy';
import { pluginReactLynx } from '@lynx-js/react-rsbuild-plugin';
export default defineConfig({
  plugins: [pluginReactLynx()],
  source: { entry: './src/index.tsx' },
  output: { filename: 'main.lynx.bundle' },
});
