#!/usr/bin/env bash
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.
set -euo pipefail
text_root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$text_root/out/Wasm"
cd "$text_root/platform/wasm/app"
npm ci --ignore-scripts --no-audit --no-fund
LYNX_CANVAS_TEXT=1 npm run build
cp dist/main.lynx.bundle "$text_root/out/Wasm/text.lynx.bundle"
cd "$text_root"
python3 tools/error_code/gen_error_code.py -l ts
node platform/wasm/build-core-js.mjs
PROBE_TARGET=bundle_canvas PROBE_SITE=out/Wasm/text-site bash platform/wasm/build.sh
