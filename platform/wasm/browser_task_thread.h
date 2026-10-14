// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_BROWSER_TASK_THREAD_H_
#define PLATFORM_WASM_BROWSER_TASK_THREAD_H_
#include "base/include/fml/task_runner.h"
namespace lynx::wasm {
// Creates a persistent pthread event loop without waiting on the browser UI.
// The returned runner queues tasks until the worker initializes its FML loop.
fml::RefPtr<fml::TaskRunner> CreateBrowserTaskThread();
}  // namespace lynx::wasm
#endif
