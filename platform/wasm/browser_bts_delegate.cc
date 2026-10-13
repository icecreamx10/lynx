// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include "platform/wasm/static_bundle_host.h"
namespace lynx::tasm {
void StaticBundleDelegate::StartBackground(int id) {
  if (!prepared) return;
  // All QJS initialization, script execution and teardown occur on the worker.
  background = std::make_unique<BrowserBTSWorker>(this, id);
  for (auto& event : pending_messages)
    background->OnReceiveMessageEvent(std::move(event));
  pending_messages.clear();
}
}  // namespace lynx::tasm
