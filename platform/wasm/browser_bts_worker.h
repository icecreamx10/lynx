// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_BROWSER_BTS_WORKER_H_
#define PLATFORM_WASM_BROWSER_BTS_WORKER_H_
#include <atomic>
#include <memory>
#include <string>

#include "base/include/closure.h"
#include "base/include/fml/task_runner.h"
#include "core/runtime/common/bindings/event/message_event.h"
#include "core/runtime/js/runtime_constant.h"
#include "core/shell/runtime/bts/bts_runtime.h"
#include "platform/wasm/browser_runtime.h"
namespace lynx::tasm {
class StaticBundleDelegate;
// One persistent pthread/Web Worker per WASM instance; a page owns an actor
// whose runtime and callbacks are created/destroyed only on that worker.
void PostToBTSWorker(base::closure task);
uint32_t BTSWorkerThreadId();
class BrowserBTSWorker {
 public:
  struct State;
  explicit BrowserBTSWorker(StaticBundleDelegate* host, int id);
  ~BrowserBTSWorker();
  void OnReceiveMessageEvent(fml::RefPtr<runtime::MessageEvent> event);
  void CallJSApiCallback(runtime::js::ApiCallBack callback);
  void CallJSApiCallbackWithValue(runtime::js::ApiCallBack callback,
                                  const lepus::Value& value, bool persist);
  void CallJSFunction(const std::string& module, const std::string& method,
                      const lepus::Value& args);
  void Tick(int64_t time);
  size_t Pending() const;
  void TestBusy(int milliseconds);
  bool Busy() const;

 private:
  std::shared_ptr<State> state_;
};
}  // namespace lynx::tasm
#endif
