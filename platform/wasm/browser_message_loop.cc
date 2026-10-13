// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include <emscripten.h>

#include <algorithm>

#include "base/include/fml/message_loop_impl.h"
namespace lynx::fml {
// Browser-owned event loop: FML tasks run from browser timers, never a
// blocking POSIX loop. The callback retains the loop until it has fired.
class BrowserMessageLoop final : public MessageLoopImpl {
 public:
  void Run() override { std::abort(); }
  void Terminate() override { stopped_ = true; }
  void WakeUp(TimePoint time) override {
    if (stopped_ || time == TimePoint::Max()) return;
    auto* retained = new RefPtr<BrowserMessageLoop>(this);
    double delay = std::max(0.0, (time - TimePoint::Now()).ToMillisecondsF());
    emscripten_async_call(
        [](void* data) {
          std::unique_ptr<RefPtr<BrowserMessageLoop>> loop(
              static_cast<RefPtr<BrowserMessageLoop>*>(data));
          if (!(*loop)->stopped_) (*loop)->RunExpiredTasksNow();
        },
        retained, static_cast<int>(std::min(delay, 2147483647.0)));
  }

 private:
  bool stopped_ = false;
};
RefPtr<MessageLoopImpl> MessageLoopImpl::Create(void*) {
  return AdoptRef<MessageLoopImpl>(new BrowserMessageLoop());
}
}  // namespace lynx::fml
