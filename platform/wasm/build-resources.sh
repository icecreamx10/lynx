#!/usr/bin/env bash
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.
set -euo pipefail
resource_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$resource_root"
# One full build, then relink the fixture-specific embedded bundle for each
# candidate. Nothing is promoted to the live preview by this script.
bash platform/wasm/build-text.sh
source "$LYNX_WASM_EMSDK/emsdk_env.sh" >/dev/null 2>&1
cp out/Wasm/bundle_canvas.js out/Wasm/text-resource-module.js
cd platform/wasm/app
LYNX_CANVAS_TEXT=0 LYNX_CANVAS_IMAGE_LIFECYCLE=1 npm run build
cp dist/main.lynx.bundle "$resource_root/out/Wasm/image-lifecycle.lynx.bundle"
LYNX_CANVAS_TEXT=0 LYNX_CANVAS_IMAGE_LIFECYCLE=0 LYNX_CANVAS_IMAGES=1 npm run build
cp dist/main.lynx.bundle "$resource_root/out/Wasm/images-resource.lynx.bundle"
cd "$resource_root"
PYTHONHASHSEED=0 ninja -C out/Wasm -j "${PROBE_JOBS:-8}" platform/wasm:bundle_canvas
cp out/Wasm/bundle_canvas.js out/Wasm/images-resource-module.js
python3 platform/wasm/stage.py --target bundle_canvas --site out/Wasm/text-site/images
cd platform/wasm/app
LYNX_CANVAS_TEXT=0 LYNX_CANVAS_IMAGE_LIFECYCLE=0 LYNX_CANVAS_IMAGES=0 LYNX_CANVAS_INTERACTIVE=1 npm run build
cp dist/main.lynx.bundle "$resource_root/out/Wasm/interactive-resource.lynx.bundle"
cp dist/main.lynx.bundle "$resource_root/out/Wasm/interactive-worker.lynx.bundle"
cd "$resource_root"
PYTHONHASHSEED=0 ninja -C out/Wasm -j "${PROBE_JOBS:-8}" platform/wasm:bundle_canvas
cp out/Wasm/bundle_canvas.js out/Wasm/interactive-resource-module.js
python3 platform/wasm/stage.py --target bundle_canvas --site out/Wasm/text-site/interactive
cp out/Wasm/text.lynx.bundle platform/wasm/app/dist/main.lynx.bundle
PYTHONHASHSEED=0 ninja -C out/Wasm -j "${PROBE_JOBS:-8}" platform/wasm:bundle_canvas
cp out/Wasm/bundle_canvas.js out/Wasm/text-resource-module.js
python3 platform/wasm/stage.py --target bundle_canvas --site out/Wasm/text-site
