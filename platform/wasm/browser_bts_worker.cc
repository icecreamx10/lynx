// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include "platform/wasm/browser_bts_worker.h"

#include <emscripten.h>
#include <pthread.h>

#include <mutex>
#include <vector>

#include "base/include/fml/message_loop.h"
#include "core/runtime/common/bindings/modules/lynx_native_module_manager.h"
#include "platform/wasm/browser_bts_delegate.h"
namespace lynx::tasm {
namespace {
struct WorkerThread {
  std::mutex mutex;
  fml::RefPtr<fml::TaskRunner> runner;
  std::vector<base::closure> queued;
  std::atomic<uint32_t> id{0};
  WorkerThread() {
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
    int result = pthread_create(
        &thread, &attr,
        [](void* arg) -> void* {
          auto* self = static_cast<WorkerThread*>(arg);
          auto runner = fml::MessageLoop::EnsureInitializedForCurrentThread()
                            .GetTaskRunner();
          self->id = reinterpret_cast<uintptr_t>(pthread_self());
          {
            std::lock_guard<std::mutex> lock(self->mutex);
            self->runner = runner;
            for (auto& task : self->queued) runner->PostTask(std::move(task));
            self->queued.clear();
          }
          // Keep the pthread's event loop alive without blocking it or the UI.
          emscripten_exit_with_live_runtime();
          return nullptr;
        },
        this);
    pthread_attr_destroy(&attr);
    if (result != 0) std::abort();
    pthread_detach(thread);
  }
};
WorkerThread& Thread() {
  static auto* thread = new WorkerThread();
  return *thread;
}
}  // namespace
void PostToBTSWorker(base::closure task) {
  auto& thread = Thread();
  std::lock_guard<std::mutex> lock(thread.mutex);
  if (thread.runner)
    thread.runner->PostTask(std::move(task));
  else
    thread.queued.push_back(std::move(task));
}
uint32_t BTSWorkerThreadId() { return Thread().id; }
struct BrowserBTSWorker::State {
  std::unique_ptr<shell::BTSRuntime> runtime;
  std::shared_ptr<BrowserFrameClock> clock =
      std::make_shared<BrowserFrameClock>();
  std::atomic<bool> alive{true}, frame_pending{false}, busy{false};
  std::atomic<int64_t> latest_frame{0};
};
BrowserBTSWorker::BrowserBTSWorker(StaticBundleDelegate* host, int id)
    : state_(std::make_shared<State>()) {
  auto script = std::move(host->prepared);
  // A worker never receives values backed by the MTS QuickJS context.
  script->bundle.encoded_data =
      lepus::Value::Clone(script->bundle.encoded_data).ToLepusValue(true);
  script->bundle.custom_sections =
      lepus::Value::Clone(script->bundle.custom_sections).ToLepusValue(true);
  script->bundle.init_data.SetValue(
      lepus::Value::Clone(script->bundle.init_data.GetValue())
          .ToLepusValue(true));
  for (auto& data : script->bundle.cache_data)
    data.SetValue(lepus::Value::Clone(data.GetValue()).ToLepusValue(true));
  auto main_runner = fml::MessageLoop::GetCurrent().GetTaskRunner();
  auto life = host->alive;
  PostToBTSWorker([state = state_, host, life, id, main_runner,
                   script = std::move(script)]() mutable {
    if (!state->alive || !*life) return;
    auto backend = std::make_unique<runtime::BrowserBTSDelegate>(
        host, life, main_runner, state->clock);
    PageOptions options(id);
    options.SetLongTaskMonitorDisabled(true);
    options.SetHasLogicExecutor(true);
    state->runtime = std::make_unique<shell::BTSRuntime>(
        "wasm-worker-page-" + std::to_string(id), base::LogContext{}, id,
        std::move(backend), "", shell::FORCE_USE_LIGHT_WEIGHT_JS_ENGINE,
        options);
    state->runtime->Init(std::make_shared<pub::LynxNativeModuleManager>(),
                         nullptr, {});
    state->runtime->OnJSSourcePrepared(
        std::move(script->bundle), script->props, script->page, script->dsl,
        script->mode, script->url, script->options, script->trace);
  });
}
BrowserBTSWorker::~BrowserBTSWorker() {
  state_->alive = false;
  PostToBTSWorker([state = state_]() mutable {
    state->runtime.reset();
    state->clock.reset();
  });
}
void BrowserBTSWorker::OnReceiveMessageEvent(
    fml::RefPtr<runtime::MessageEvent> event) {
  PostToBTSWorker([state = state_, event = std::move(event)]() mutable {
    if (state->alive && state->runtime)
      state->runtime->OnReceiveMessageEvent(std::move(event));
  });
}
void BrowserBTSWorker::CallJSApiCallback(runtime::js::ApiCallBack callback) {
  PostToBTSWorker([state = state_, callback] {
    if (state->alive && state->runtime)
      state->runtime->CallJSApiCallback(callback);
  });
}
void BrowserBTSWorker::CallJSApiCallbackWithValue(
    runtime::js::ApiCallBack callback, const lepus::Value& value,
    bool persist) {
  auto native = lepus::Value::Clone(value).ToLepusValue(true);
  PostToBTSWorker(
      [state = state_, callback, value = std::move(native), persist] {
        if (state->alive && state->runtime)
          state->runtime->CallJSApiCallbackWithValue(callback, value, persist);
      });
}
void BrowserBTSWorker::CallJSFunction(const std::string& module,
                                      const std::string& method,
                                      const lepus::Value& args) {
  auto native = lepus::Value::Clone(args).ToLepusValue(true);
  PostToBTSWorker([state = state_, module, method, args = std::move(native)] {
    if (state->alive && state->runtime)
      state->runtime->CallJSFunction(module, method, args);
  });
}
void BrowserBTSWorker::Tick(int64_t time) {
  state_->latest_frame = time;
  if (state_->frame_pending.exchange(true)) return;
  PostToBTSWorker([state = state_] {
    state->frame_pending = false;
    if (state->alive && state->clock) state->clock->Tick(state->latest_frame);
  });
}
size_t BrowserBTSWorker::Pending() const {
  return state_->clock ? state_->clock->Pending() : 0;
}
bool BrowserBTSWorker::Busy() const { return state_->busy; }
void BrowserBTSWorker::TestBusy(int milliseconds) {
  PostToBTSWorker([state = state_, milliseconds] {
    if (!state->alive || !state->runtime) return;
    state->busy = true;
    auto* rt = state->runtime->GetJSRuntimeWeak().Lock();
    if (rt) {
      runtime::js::Scope scope(*rt);
      auto result = rt->evaluateJavaScript(
          std::make_shared<runtime::js::StringBuffer>(
              "{let end=Date.now()+" + std::to_string(milliseconds) +
              ";while(Date.now()<end){};}"),
          "worker-acceptance.js");
      if (!result.has_value()) rt->reportJSIException(result.error());
    }
    state->busy = false;
  });
}
}  // namespace lynx::tasm
