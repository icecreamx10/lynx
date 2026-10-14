#!/usr/bin/env bash
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

set -euo pipefail
export PYTHONHASHSEED=0
probe_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$probe_root"
: "${LYNX_WASM_EMSDK:?Set LYNX_WASM_EMSDK to an activated Emscripten 4.0.20 SDK}"
source "$LYNX_WASM_EMSDK/emsdk_env.sh" >/dev/null 2>&1
python3 platform/wasm/prepare.py
probe_thread_flags=""
if [[ "${PROBE_TARGET:-static_canvas}" == "bundle_canvas" ]]; then probe_thread_flags="-pthread"; fi
if [[ "${PROBE_TARGET:-static_canvas}" == "bundle_canvas" ]]; then
  python3 tools/error_code/gen_error_code.py -l cpp
  python3 tools/feature_count/generate_feature_count.py
fi
emcmake cmake -S out/skity-source -B out/skity-wasm -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_C_FLAGS="$probe_thread_flags" -DCMAKE_CXX_FLAGS="$probe_thread_flags" \
  -DSKITY_GL_BACKEND=OFF -DSKITY_MTL_BACKEND=OFF -DSKITY_VK_BACKEND=OFF \
  -DSKITY_CODEC_MODULE=OFF -DSKITY_IO_MODULE=OFF -DSKITY_SW_RENDERER=ON
cmake --build out/skity-wasm -j "${PROBE_JOBS:-8}"
if [[ "${PROBE_TARGET:-static_canvas}" == "bundle_canvas" ]]; then
  emcmake cmake -S platform/wasm/textra -B out/textra-wasm -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DTEXTRA_SOURCE="$probe_root/out/textra-source"
  cmake --build out/textra-wasm -j "${PROBE_JOBS:-8}"
fi
mkdir -p out/Wasm
python3 - <<'PY'
import json
import os
from pathlib import Path
sdk = str(Path(os.environ['LYNX_WASM_EMSDK']).resolve())
threaded = os.environ.get('PROBE_TARGET') == 'bundle_canvas'
inspector = os.environ.get('PROBE_INSPECTOR') == '1'
Path('out/Wasm/args.gn').write_text(('''target_cpu = "wasm"
is_debug = false
jsengine_type = "quickjs"
enable_v8 = false
enable_clay = false
enable_inspector = INSPECTOR_ENABLED
enable_napi_binding = false
enable_recorder = false
enable_logbox = false
use_sysroot = false
enable_lto = false
enable_wasm_static_canvas = true
emsdk_dir = ''' + json.dumps(sdk) + '\nwasm_enable_pthreads = ' + str(threaded).lower() + '\n').replace('INSPECTOR_ENABLED', str(inspector).lower()))
PY
buildtools/gn/gn gen out/Wasm
probe_ninja="$(command -v ninja || true)"
if [[ -z "$probe_ninja" ]]; then probe_ninja="$probe_root/buildtools/ninja/ninja"; fi
"$probe_ninja" -C out/Wasm -j "${PROBE_JOBS:-8}" "platform/wasm:${PROBE_TARGET:-static_canvas}"
if [[ "${PROBE_INSPECTOR:-0}" == "1" ]]; then
  python3 platform/wasm/stage.py --target "${PROBE_TARGET:-static_canvas}" --site "${PROBE_SITE:-out/Wasm/site}" --devtool
  python3 platform/wasm/devtool/prepare.py --site "${PROBE_SITE:-out/Wasm/site}"
else
  python3 platform/wasm/stage.py --target "${PROBE_TARGET:-static_canvas}" --site "${PROBE_SITE:-out/Wasm/site}"
fi
printf 'Preview: python3 platform/wasm/serve.py\n'
