// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include <emscripten.h>
#include <emscripten/proxying.h>
#include <pthread.h>

#include <algorithm>

#include "base/include/fml/message_loop_impl.h"
namespace lynx::fml {
// Each loop belongs to its creating pthread. Cross-thread wakeups only schedule
// a timer on that owner; user tasks never execute inside the system proxy
// queue.
class BrowserMessageLoop final : public MessageLoopImpl {
 public:
  void Run() override { std::abort(); }
  void Terminate() override { stopped_ = true; }
  void WakeUp(TimePoint time) override {
    if (stopped_ || time == TimePoint::Max()) return;
    struct Wake {
      RefPtr<BrowserMessageLoop> loop;
      int delay;
    };
    auto* retained =
        new Wake{RefPtr<BrowserMessageLoop>(this),
                 static_cast<int>(std::min(
                     std::max(0.0, (time - TimePoint::Now()).ToMillisecondsF()),
                     2147483647.0))};
    auto schedule = [](void* data) {
      auto* wake = static_cast<Wake*>(data);
      emscripten_async_call(
          [](void* data) {
            std::unique_ptr<Wake> wake(static_cast<Wake*>(data));
            if (!wake->loop->stopped_) wake->loop->RunExpiredTasksNow();
          },
          wake, wake->delay);
    };
    if (pthread_equal(owner_, pthread_self()))
      schedule(retained);
    else if (!emscripten_proxy_async(emscripten_proxy_get_system_queue(),
                                     owner_, schedule, retained))
      delete retained;
  }

 private:
  pthread_t owner_ = pthread_self();
  std::atomic<bool> stopped_{false};
};
RefPtr<MessageLoopImpl> MessageLoopImpl::Create(void*) {
  return AdoptRef<MessageLoopImpl>(new BrowserMessageLoop());
}
}  // namespace lynx::fml
