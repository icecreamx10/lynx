// Copyright 2024 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef DEVTOOL_LYNX_DEVTOOL_AGENT_LYNX_DEVTOOL_MEDIATOR_BASE_H_
#define DEVTOOL_LYNX_DEVTOOL_AGENT_LYNX_DEVTOOL_MEDIATOR_BASE_H_

#include <memory>

#include "base/include/fml/thread.h"
#if defined(__EMSCRIPTEN__)
#include "core/base/threading/task_runner_manufactor.h"
#endif
#include "base/include/log/logging.h"
#include "devtool/lynx_devtool/agent/inspector_util.h"

namespace lynx {
namespace devtool {

// Why LynxDevToolMediator and LynxGlobalDevToolMediator?
// The life cycle of LynxGlobalDevToolMediator is associated with App,
// it is created once pre App. Each view contains a LynxDevToolMediator
// ,which is created after view opened. For the testbrench event that need to
// handle before view opened, LynxGlobalDevToolMediator is needed.

class LynxDevToolMediatorBase {
 public:
  explicit LynxDevToolMediatorBase(
      fml::RefPtr<fml::TaskRunner> devtool_runner = nullptr,
      fml::RefPtr<fml::TaskRunner> event_runner = nullptr) {
#if defined(__EMSCRIPTEN__)
    // Browser global services default to the existing UI event loop. Creating
    // a native FML thread here would synchronously wait on the browser UI.
    if (!devtool_runner) devtool_runner = base::UIThread::GetRunner();
    if (!event_runner) event_runner = base::UIThread::GetRunner();
#endif
    default_task_runner_ =
        devtool_runner ? devtool_runner : GetDevToolsThread().GetTaskRunner();
    cdp_event_listener_runner_ =
        event_runner ? event_runner
                     : GetCDPEventListenerThread().GetTaskRunner();
  }
  virtual ~LynxDevToolMediatorBase() = default;

  static fml::Thread& GetDevToolsThread() {
    static base::NoDestructor<fml::Thread> devtools_thread(
        fml::Thread::ThreadConfig("devtool",
                                  fml::Thread::ThreadPriority::NORMAL));
    return *devtools_thread;
  }

  static fml::Thread& GetCDPEventListenerThread() {
    static base::NoDestructor<fml::Thread> cdp_event_listener_thread(
        fml::Thread::ThreadConfig("cdp_event_listener",
                                  fml::Thread::ThreadPriority::NORMAL));
    return *cdp_event_listener_thread;
  }

  void RunOnTaskRunner(lynx::fml::RefPtr<lynx::fml::TaskRunner>& runner,
                       lynx::base::closure&& closure, bool run_now = true) {
    if (run_now) {
      lynx::fml::TaskRunner::RunNowOrPostTask(runner, std::move(closure));
    } else {
      CHECK_NULL_AND_LOG_RETURN(runner, "runner is null");
      runner->PostTask(std::move(closure));
    }
  }

 protected:
  fml::RefPtr<fml::TaskRunner> default_task_runner_;
  fml::RefPtr<fml::TaskRunner> cdp_event_listener_runner_;
};

}  // namespace devtool
}  // namespace lynx

#endif  // DEVTOOL_LYNX_DEVTOOL_AGENT_LYNX_DEVTOOL_MEDIATOR_BASE_H_
