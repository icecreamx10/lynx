#!/usr/bin/env bash
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.
set -euo pipefail
image_root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$image_root/out/Wasm"
cd "$image_root/platform/wasm/app"
npm ci --ignore-scripts --no-audit --no-fund
LYNX_CANVAS_IMAGE_LIFECYCLE=1 npm run build
cp dist/main.lynx.bundle "$image_root/out/Wasm/image-lifecycle.lynx.bundle"
LYNX_CANVAS_IMAGE_LIFECYCLE=0 LYNX_CANVAS_IMAGES=0 LYNX_CANVAS_INTERACTIVE=1 npm run build
cp dist/main.lynx.bundle "$image_root/out/Wasm/interactive-worker.lynx.bundle"
LYNX_CANVAS_IMAGE_LIFECYCLE=0 LYNX_CANVAS_IMAGES=1 npm run build
cd "$image_root"
python3 tools/error_code/gen_error_code.py -l ts
node platform/wasm/build-core-js.mjs
PROBE_TARGET=bundle_canvas PROBE_SITE=out/Wasm/images-site bash platform/wasm/build.sh
