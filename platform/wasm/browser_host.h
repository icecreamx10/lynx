// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_BROWSER_HOST_H_
#define PLATFORM_WASM_BROWSER_HOST_H_
#include <string>
namespace lynx::wasm {
bool IsRenderWorker();
void SendHostMessage(const std::string& kind, int id, const std::string& json);
}  // namespace lynx::wasm
#endif
