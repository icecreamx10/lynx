// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include "platform/wasm/browser_task_thread.h"

#include <emscripten.h>
#include <pthread.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "base/include/fml/message_loop.h"
#include "base/include/fml/message_loop_impl.h"
namespace lynx::wasm {
namespace {
class BrowserTaskRunner final : public fml::TaskRunner {
 public:
  BrowserTaskRunner() : TaskRunner(nullptr) {
    // Browser task threads live for the WASM instance. Keep their runner alive
    // after pthread entry returns to the browser event loop.
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
    const int result = pthread_create(
        &thread, &attr,
        [](void* data) -> void* {
          auto* self = static_cast<BrowserTaskRunner*>(data);
          auto runner = fml::MessageLoop::EnsureInitializedForCurrentThread()
                            .GetTaskRunner();
          self->owner_ = reinterpret_cast<uintptr_t>(pthread_self());
          {
            std::lock_guard<std::mutex> lock(self->mutex_);
            self->runner_ = runner;
            for (auto& task : self->queued_) runner->PostTask(std::move(task));
            self->queued_.clear();
          }
          emscripten_exit_with_live_runtime();
          return nullptr;
        },
        this);
    pthread_attr_destroy(&attr);
    if (result != 0) std::abort();
    pthread_detach(thread);
  }
  void PostTask(base::closure task) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (runner_)
      runner_->PostTask(std::move(task));
    else
      queued_.push_back(std::move(task));
  }
  bool RunsTasksOnCurrentThread() override {
    return owner_ == reinterpret_cast<uintptr_t>(pthread_self());
  }
  void PostTaskForTime(base::closure task, fml::TimePoint time) override {
    PostTask([task = std::move(task), time]() mutable {
      fml::MessageLoop::GetCurrent().GetTaskRunner()->PostTaskForTime(
          std::move(task), time);
    });
  }
  void PostDelayedTask(base::closure task, fml::TimeDelta delay) override {
    PostTaskForTime(std::move(task), fml::TimePoint::Now() + delay);
  }

 private:
  std::mutex mutex_;
  std::vector<base::closure> queued_;
  fml::RefPtr<fml::TaskRunner> runner_;
  std::atomic<uintptr_t> owner_{0};
};
}  // namespace
fml::RefPtr<fml::TaskRunner> CreateBrowserTaskThread() {
  auto runner = fml::AdoptRef<fml::TaskRunner>(new BrowserTaskRunner());
  runner->AddRef();
  return runner;
}
}  // namespace lynx::wasm
