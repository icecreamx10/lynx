// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include "platform/wasm/browser_bts_delegate.h"

#include "core/runtime/common/bindings/modules/lynx_native_module_manager.h"
namespace lynx::tasm {
void StaticBundleDelegate::StartBackground(int id) {
  if (!prepared) return;
  auto backend = std::make_unique<runtime::BrowserBTSDelegate>(this);
  PageOptions options(id);
  options.SetLongTaskMonitorDisabled(true);
  options.SetHasLogicExecutor(true);
  background = std::make_unique<shell::BTSRuntime>(
      "wasm-page-" + std::to_string(id), base::LogContext{}, id,
      std::move(backend), "", shell::FORCE_USE_LIGHT_WEIGHT_JS_ENGINE, options);
  background->Init(std::make_shared<pub::LynxNativeModuleManager>(), nullptr,
                   {});
  auto script = std::move(prepared);
  background->OnJSSourcePrepared(std::move(script->bundle), script->props,
                                 script->page, script->dsl, script->mode,
                                 script->url, script->options, script->trace);
  for (auto& event : pending_messages)
    Post([this, event = std::move(event)]() mutable {
      background->OnReceiveMessageEvent(std::move(event));
    });
  pending_messages.clear();
}
}  // namespace lynx::tasm
