// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include <emscripten/html5.h>

#include <memory>
#include <string>

#include "core/base/threading/vsync_monitor.h"
#include "core/renderer/tasm/config.h"
#include "core/renderer/utils/lynx_env.h"
#include "core/runtime/js/bytecode/js_cache_manager.h"

namespace lynx::fml {
// Browser main-thread names are not mutable through POSIX APIs.
void SetThreadName(const std::string&) {}
}  // namespace lynx::fml
namespace lynx::tasm {
const char* Config::Platform() { return "Web"; }
}  // namespace lynx::tasm
namespace lynx::base {
class BrowserVSyncMonitor final : public VSyncMonitor {
 public:
  explicit BrowserVSyncMonitor(bool on_ui) : VSyncMonitor(on_ui, false) {}
  void RequestVSyncOnUIThread() override { RequestVSync(); }

 protected:
  void RequestVSync() override {
    auto* weak = new std::weak_ptr<VSyncMonitor>(weak_from_this());
    emscripten_request_animation_frame(
        [](double time, void* data) -> EM_BOOL {
          std::unique_ptr<std::weak_ptr<VSyncMonitor>> weak(
              static_cast<std::weak_ptr<VSyncMonitor>*>(data));
          if (auto monitor = weak->lock()) {
            const int64_t start = static_cast<int64_t>(time * 1000000);
            // Static host uses a nominal frame budget; adaptive refresh-rate
            // measurement remains part of the interactive host work.
            monitor->OnVSync(start, start + 16666667);
          }
          return EM_FALSE;
        },
        weak);
  }
};
std::shared_ptr<VSyncMonitor> VSyncMonitor::Create(bool on_ui) {
  return std::make_shared<BrowserVSyncMonitor>(on_ui);
}
}  // namespace lynx::base

namespace lynx::tasm {
bool LynxEnv::IsJsDebugEnabled(bool, bool) { return false; }
}  // namespace lynx::tasm
namespace lynx::runtime::js::cache {
std::string JsCacheManager::GetPlatformCacheDir() { return "/tmp"; }
}  // namespace lynx::runtime::js::cache
