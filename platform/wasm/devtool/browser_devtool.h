// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_DEVTOOL_BROWSER_DEVTOOL_H_
#define PLATFORM_WASM_DEVTOOL_BROWSER_DEVTOOL_H_
#include "base/include/fml/message_loop_impl.h"
#include "devtool/lynx_devtool/lynx_devtool_ng.h"
#include "platform/wasm/browser_bts_worker.h"
#include "platform/wasm/browser_task_thread.h"

namespace lynx::wasm {
// Owns only host integration. Protocol agents, Console, target routing and
// pause/resume remain in the existing Lynx DevTool backend.
inline fml::RefPtr<fml::TaskRunner> BrowserDevToolRunner() {
  static auto runner = CreateBrowserTaskThread();
  return runner;
}
class PageTaskRunner final : public fml::TaskRunner {
 public:
  PageTaskRunner(fml::RefPtr<fml::TaskRunner> runner,
                 std::shared_ptr<std::atomic<bool>> alive)
      : TaskRunner(nullptr), runner_(runner), alive_(std::move(alive)) {}
  void PostTask(base::closure task) override {
    runner_->PostTask(Guard(std::move(task)));
  }
  void PostTaskForTime(base::closure task, fml::TimePoint time) override {
    runner_->PostTaskForTime(Guard(std::move(task)), time);
  }
  void PostDelayedTask(base::closure task, fml::TimeDelta delay) override {
    runner_->PostDelayedTask(Guard(std::move(task)), delay);
  }
  bool RunsTasksOnCurrentThread() override {
    return runner_->RunsTasksOnCurrentThread();
  }

 private:
  base::closure Guard(base::closure task) {
    return [alive = alive_, task = std::move(task)]() mutable {
      if (*alive) task();
    };
  }
  fml::RefPtr<fml::TaskRunner> runner_;
  std::shared_ptr<std::atomic<bool>> alive_;
};
class BrowserDevTool final : public devtool::LynxDevToolNG {
 public:
  BrowserDevTool()
      : LynxDevToolNG(true,
                      std::make_shared<devtool::LynxDevToolMediator>(
                          BrowserDevToolRunner(), BrowserDevToolRunner())) {}
  std::shared_ptr<runtime::js::InspectorRuntimeObserverNG> Init(
      tasm::TemplateAssembler* assembler,
      std::shared_ptr<std::atomic<bool>> alive) {
    const auto main = fml::AdoptRef<fml::TaskRunner>(
        new PageTaskRunner(base::UIThread::GetRunner(), std::move(alive)));
    return devtool_mediator_->Init(
        {assembler, main, tasm::BTSWorkerTaskRunner(), main, true, 0, nullptr},
        shared_from_this());
  }
};
}  // namespace lynx::wasm
#endif
