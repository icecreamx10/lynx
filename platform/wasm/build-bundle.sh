#!/usr/bin/env bash
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.
set -euo pipefail
bundle_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$bundle_root/platform/wasm/app"
npm ci --ignore-scripts --no-audit --no-fund
npm run build
cd "$bundle_root"
# Stage a candidate separately from the live preview until browser checks pass.
PROBE_TARGET=bundle_canvas PROBE_SITE=out/Wasm/bundle-site bash platform/wasm/build.sh
