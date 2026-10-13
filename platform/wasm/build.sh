#!/usr/bin/env bash
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

set -euo pipefail
probe_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$probe_root"
: "${LYNX_WASM_EMSDK:?Set LYNX_WASM_EMSDK to an activated Emscripten 4.0.20 SDK}"
source "$LYNX_WASM_EMSDK/emsdk_env.sh" >/dev/null 2>&1
python3 platform/wasm/prepare.py
emcmake cmake -S out/skity-source -B out/skity-wasm -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DSKITY_GL_BACKEND=OFF -DSKITY_MTL_BACKEND=OFF -DSKITY_VK_BACKEND=OFF \
  -DSKITY_CODEC_MODULE=OFF -DSKITY_IO_MODULE=OFF -DSKITY_SW_RENDERER=ON
cmake --build out/skity-wasm -j "${PROBE_JOBS:-8}"
mkdir -p out/Wasm
python3 - <<'PY'
import json
import os
from pathlib import Path
sdk = str(Path(os.environ['LYNX_WASM_EMSDK']).resolve())
Path('out/Wasm/args.gn').write_text('''target_cpu = "wasm"
is_debug = false
jsengine_type = "quickjs"
enable_v8 = false
enable_clay = false
enable_inspector = false
enable_napi_binding = false
enable_recorder = false
enable_logbox = false
use_sysroot = false
enable_lto = false
enable_wasm_static_canvas = true
emsdk_dir = ''' + json.dumps(sdk) + '\n')
PY
buildtools/gn/gn gen out/Wasm
probe_ninja="$(command -v ninja || true)"
if [[ -z "$probe_ninja" ]]; then probe_ninja="$probe_root/buildtools/ninja/ninja"; fi
"$probe_ninja" -C out/Wasm -j "${PROBE_JOBS:-8}" platform/wasm:static_canvas
cp platform/wasm/index.html out/Wasm/index.html
cp out/Roboto-OFL.txt out/Wasm/Roboto-OFL.txt
mkdir -p out/Wasm/site
cp out/Wasm/static_canvas.js out/Wasm/static_canvas.wasm out/Wasm/static_canvas.data \
  platform/wasm/index.html out/Roboto-OFL.txt out/Wasm/site/
printf 'Preview: python3 platform/wasm/serve.py\n'
