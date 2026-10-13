// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_BROWSER_BTS_DELEGATE_H_
#define PLATFORM_WASM_BROWSER_BTS_DELEGATE_H_
#include <fstream>

#include "core/runtime/common/bindings/event/message_event.h"
#include "core/runtime/js/template_delegate.h"
#include "platform/wasm/static_bundle_host.h"
namespace lynx::runtime {
class BrowserBTSDelegate : public TemplateDelegate {
 public:
  BrowserBTSDelegate(tasm::StaticBundleDelegate* host,
                     std::shared_ptr<std::atomic<bool>> life,
                     fml::RefPtr<fml::TaskRunner> main_runner,
                     std::shared_ptr<tasm::BrowserFrameClock> clock)
      : host_(host),
        host_alive_(std::move(life)),
        main_runner_(std::move(main_runner)),
        clock_(std::move(clock)) {}
  ~BrowserBTSDelegate() override { *alive_ = false; }
  void Post(base::closure task) {
    fml::MessageLoop::GetCurrent().GetTaskRunner()->PostTask(
        [alive = alive_, task = std::move(task)]() mutable {
          if (*alive) task();
        });
  }
  void PostMain(base::closure task) {
    main_runner_->PostTask(
        [life = host_alive_, task = std::move(task)]() mutable {
          if (*life) task();
        });
  }
  static std::string ReadCore() {
    std::ifstream file("/lynx_core.js");
    return std::string(std::istreambuf_iterator<char>(file), {});
  }
  void OnRuntimeGC(
      std::unordered_map<std::string, std::string> mem_info) override {}
  void UpdateDataByJS(UpdateDataTask task) override {
    task.data_ = lepus::Value::Clone(task.data_).ToLepusValue(true);
    PostMain([host = host_, task = std::move(task)]() mutable {
      if (host->owner) {
        auto options = task.pipeline_options_;
        host->owner->UpdateDataByJS(task, options);
      }
    });
  }
  void UpdateBatchedDataByJS(std::vector<UpdateDataTask> tasks,
                             uint64_t update_task_id) override {}
  std::vector<lynx::shell::CacheDataOp> FetchUpdatedCardData() override {
    return {};
  }
  runtime::js::JsContent GetJSContentFromExternal(const std::string& entry_name,
                                                  const std::string& name,
                                                  long timeout) override {
    return runtime::js::JsContent(std::string(),
                                  runtime::js::JsContent::Type::SOURCE);
  }
  std::string GetLynxJSAsset(const std::string& name) override {
    return ReadCore();
  }
  void GetComponentContextDataAsync(
      const std::string& component_id, const std::string& key,
      runtime::js::ApiCallBack callback) override {}
  bool LoadDynamicComponentFromJS(
      const std::string& url, const runtime::js::ApiCallBack& callback,
      const std::vector<std::string>& ids,
      std::optional<tasm::LynxTemplateBundle> template_bundle) override {
    return {};
  }
  void LoadScriptAsync(const std::string& url,
                       runtime::js::ApiCallBack callback) override {}
  void AddFont(const lepus::Value& font,
               const runtime::js::ApiCallBack& callback) override {}
  void FetchBundle(
      const std::string& url,
      const std::shared_ptr<runtime::ResponsePromise<tasm::BundleResourceInfo>>&
          response_promise) override {}
  void OnRuntimeReady() override {
    PostMain([host = host_] { host->background_ready = true; });
  }
  void OnErrorOccurred(base::LynxError error) override {
    PostMain([host = host_, error = std::move(error)]() mutable {
      host->OnErrorOccurred(std::move(error));
    });
  }
  void OnModuleMethodInvoked(const std::string& module,
                             const std::string& method, int32_t code) override {
  }
  void OnEvaluateJavaScriptEnd(const std::string& url) override {}
  void OnCoreJSUpdated(std::string core_js) override {}
  void UpdateComponentData(UpdateDataTask task) override {}
  void SelectComponent(const std::string& component_id,
                       const std::string& id_selector, const bool single,
                       runtime::js::ApiCallBack callBack) override {}
  void InvokeUIMethod(tasm::NodeSelectRoot root,
                      tasm::NodeSelectOptions options, std::string method,
                      fml::RefPtr<tasm::PropBundle> params,
                      runtime::js::ApiCallBack call_back) override {}
  void InvokeUIMethod(tasm::NodeSelectRoot root,
                      tasm::NodeSelectOptions options, std::string method,
                      const pub::ValueImplLepus& params,
                      runtime::js::ApiCallBack call_back) override {}
  void GetPathInfo(tasm::NodeSelectRoot root, tasm::NodeSelectOptions options,
                   runtime::js::ApiCallBack call_back) override {}
  void GetFields(tasm::NodeSelectRoot root, tasm::NodeSelectOptions options,
                 std::vector<std::string> fields,
                 runtime::js::ApiCallBack call_back) override {}
  void ElementAnimate(const std::string& component_id,
                      const std::string& id_selector,
                      const lepus::Value& args) override {}
  void ElementAnimateV2(const std::string& component_id,
                        const std::string& id_selector,
                        const lepus::Value& args) override {}
  void TriggerComponentEvent(const std::string& event_name,
                             const lepus::Value& msg) override {}
  void TriggerLepusGlobalEvent(const std::string& event_name,
                               const lepus::Value& msg) override {}
  void TriggerWorkletFunction(std::string component_id,
                              std::string worklet_module_name,
                              std::string method_name, lepus::Value args,
                              runtime::js::ApiCallBack callback) override {}
  void RunOnJSThread(base::closure closure) override {
    Post(std::move(closure));
  }
  void RunOnJSThreadWhenIdle(base::closure closure) override {
    Post(std::move(closure));
  }
  void SetTiming(tasm::Timing timing) override {}
  void SetTimingWithTimingFlag(const tasm::timing::TimingFlag& timing_flag,
                               const std::string& timestamp_key,
                               tasm::timing::TimestampUs timestamp) override {}
  void SetFrameworkExtraTimingInfo(const tasm::PipelineID& pipeline_id,
                                   const std::string& key,
                                   const std::string& value) override {}
  void FlushJSBTiming(runtime::js::NativeModuleInfo timing) override {}
  void ResetTimingBeforeReload() override {}
  void OnPipelineStart(
      const tasm::PipelineID& pipeline_id,
      const tasm::PipelineOrigin& pipeline_origin,
      tasm::timing::TimestampUs pipeline_start_timestamp) override {}
  void BindPipelineIDWithTimingFlag(
      const tasm::PipelineID& pipeline_id,
      const tasm::timing::TimingFlag& timing_flag) override {}
  void AddJSBlockingTime(uint64_t enqueue_time) override {}
  void InvokeLepusComponentCallback(const int64_t callback_id,
                                    const std::string& entry_name,
                                    const lepus::Value& data) override {
    PostMain([host = host_, callback_id, entry_name,
              data = lepus::Value::Clone(data).ToLepusValue(true)]() {
      if (host->owner)
        host->owner->InvokeLepusComponentCallback(callback_id, entry_name,
                                                  data);
    });
  }
  std::shared_ptr<runtime::IVSyncObserver> GetVSyncObserver() override {
    return clock_;
  }
  void SetCSSVariables(
      const std::string& component_id, const std::string& id_selector,
      const lepus::Value& properties,
      std::shared_ptr<tasm::PipelineOptions> pipeline_options) override {}
  void SetNativeProps(
      tasm::NodeSelectRoot root, const tasm::NodeSelectOptions& options,
      const lepus::Value& native_props,
      std::shared_ptr<tasm::PipelineOptions> pipeline_options) override {}
  void ReloadFromJS(UpdateDataTask task) override {}
  void StartRecording(const lepus::Value& value) override {}
  void StopRecording(const lepus::Value& value) override {}
  void CallLepusMethod(const std::string& method_name, lepus::Value value,
                       const runtime::js::ApiCallBack& callback) override {
    PostMain([host = host_, method_name,
              value = lepus::Value::Clone(value).ToLepusValue(true),
              callback]() mutable {
      if (host->owner)
        host->owner->CallLepusMethod(method_name, std::move(value), callback);
    });
  }
  fml::RefPtr<tasm::PropBundle> CreatePropBundle() override {
    return fml::AdoptRef<tasm::PropBundle>(new tasm::NativePropBundle());
  }
  void SetPropBundleCreator(
      const std::shared_ptr<tasm::PropBundleCreator>& creator) override {}
  void SetWhiteBoardDelegate(const std::shared_ptr<tasm::WhiteBoardDelegate>&
                                 white_board_delegate) override {}
  void AddEventListenersToWhiteBoard(
      runtime::ContextProxy* js_context_proxy) override {}
  std::string LoadJSSource(const std::string& name) override {
    return ReadCore();
  }
  std::shared_ptr<runtime::js::Buffer> LoadBytecode(
      const std::string& url) override {
    return {};
  }
  void GetSessionStorageItem(
      const std::string& key,
      const runtime::js::ApiCallBack& callback) override {}
  void SubscribeSessionStorage(
      const std::string&, double listener_id,
      const runtime::js::ApiCallBack& callback) override {}
  fml::RefPtr<fml::TaskRunner> GetJSRunner() override {
    return fml::MessageLoop::GetCurrent().GetTaskRunner();
  }

  event::DispatchEventResult DispatchMessageEvent(
      fml::RefPtr<runtime::MessageEvent> event) override {
    auto payload =
        lepus::Value::Clone(
            pub::ValueUtils::ConvertValueToLepusValue(*event->message()))
            .ToLepusValue(true);
    event = fml::MakeRefCounted<runtime::MessageEvent>(
        event->type(), event->time_stamp(), event->GetOriginType(),
        event->GetTargetType(),
        std::make_unique<pub::ValueImplLepus>(std::move(payload)));

    PostMain([host = host_, event = std::move(event)]() mutable {
      if (host->owner) host->owner->OnReceiveMessageEvent(std::move(event));
    });
    return {event::EventCancelType::kNotCanceled, true};
  }
  void InvokeResponsePromiseCallback(base::closure cb) override {
    Post(std::move(cb));
  }

 private:
  tasm::StaticBundleDelegate* host_;
  std::shared_ptr<std::atomic<bool>> host_alive_;
  fml::RefPtr<fml::TaskRunner> main_runner_;
  std::shared_ptr<tasm::BrowserFrameClock> clock_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};
}  // namespace lynx::runtime
#endif
