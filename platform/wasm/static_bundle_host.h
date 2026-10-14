// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_STATIC_BUNDLE_HOST_H_
#define PLATFORM_WASM_STATIC_BUNDLE_HOST_H_
#include "platform/wasm/browser_text.h"
#include <atomic>
#include <cstdio>

#include "base/include/fml/message_loop.h"
#include "core/renderer/dom/lynx_get_ui_result.h"
#include "core/renderer/template_assembler.h"
#include "core/renderer/ui_wrapper/common/native_prop_bundle.h"
#include "core/renderer/ui_wrapper/painting/empty/painting_context_implementation.h"
#include "core/runtime/common/bindings/event/message_event.h"
#include "core/runtime/js/runtime_constant.h"
#include "core/shell/runtime/bts/bts_runtime.h"
#include "core/value_wrapper/value_impl_lepus.h"
#include "platform/wasm/browser_bts_worker.h"
#include "platform/wasm/browser_runtime.h"
namespace lynx::tasm {
// Main-thread canvas host. BTS executes through a dedicated worker actor.
class StaticBundleDelegate : public TemplateAssembler::Delegate,
                             public ElementManager::Delegate {
 public:
  std::unordered_map<int32_t, LayoutInfoArray> GetSubTreeLayoutInfo(
      int32_t, Viewport = Viewport{}) override {
    return {};
  }
  std::shared_ptr<runtime::js::InspectorRuntimeObserverNG>
      inspector_runtime_observer;
  TemplateAssembler* owner = nullptr;
  std::shared_ptr<std::atomic<bool>> alive =
      std::make_shared<std::atomic<bool>>(true);
  std::unique_ptr<BrowserBTSWorker> background;
  std::shared_ptr<BrowserFrameClock> clock =
      std::make_shared<BrowserFrameClock>();
  bool background_ready = false;
  bool dirty = true;
  std::vector<fml::RefPtr<runtime::MessageEvent>> pending_messages;
  struct Prepared {
    TasmRuntimeBundle bundle;
    lepus::Value props;
    std::string page;
    PackageInstanceDSL dsl;
    PackageInstanceBundleModuleMode mode;
    std::string url;
    std::shared_ptr<PipelineOptions> options;
    uint64_t trace;
  };
  std::unique_ptr<Prepared> prepared;
  ~StaticBundleDelegate() override {
    *alive = false;
    owner = nullptr;
    background.reset();
  }
  void Post(base::closure cb) {
    fml::MessageLoop::GetCurrent().GetTaskRunner()->PostTask(
        [life = alive, cb = std::move(cb)]() mutable {
          if (*life) cb();
        });
  }
  void StartBackground(int id);
  bool loaded = false;
  bool background_source_prepared = false;
  std::vector<std::string> errors;
  void OnDataUpdated() override {}
  void OnTasmFinishByNative() override {}
  void OnRunPipelineFinished() override { dirty = true; }
  void OnTemplateLoaded(const std::string& url) override { loaded = true; }
  void OnSSRHydrateFinished(const std::string& url) override {}
  void OnErrorOccurred(base::LynxError error) override {
    errors.push_back(error.error_message_);
    std::fprintf(stderr, "Lynx error %d: %s\n", error.error_code_,
                 error.error_message_.c_str());
  }
  void TriggerLepusngGc(base::closure func) override { func(); }
  void OnDynamicComponentPerfReady(const lepus::Value& perf_info) override {}
  void OnConfigUpdated(const lepus::Value& data) override {}
  void OnPageConfigDecoded(
      const std::shared_ptr<tasm::PageConfig>& config) override {}
  void OnPageUpdated(bool is_first_screen) override { dirty = true; }
  std::string TranslateResourceForTheme(const std::string& res_id,
                                        const std::string& theme_key) override {
    return {};
  }
  void GetI18nResource(const std::string& key,
                       const std::string& fallback_url) override {}
  void SetTiming(tasm::Timing timing) override {}
  void BindPipelineIDWithTimingFlag(
      const tasm::PipelineID& pipeline_id,
      const tasm::timing::TimingFlag& timing_flag) override {}
  void OnPipelineStart(
      const tasm::PipelineID& pipeline_id,
      const tasm::PipelineOrigin& pipeline_origin,
      tasm::timing::TimestampUs pipeline_start_timestamp) override {}
  void CallJSApiCallback(runtime::js::ApiCallBack callback) override {
    if (background)
      Post([this, callback]() { background->CallJSApiCallback(callback); });
  }
  void CallJSApiCallbackWithValue(runtime::js::ApiCallBack callback,
                                  const lepus::Value& value,
                                  bool persist = false) override {
    if (background)
      Post([this, callback,
            value = lepus::Value::Clone(value).ToLepusValue(true), persist]() {
        background->CallJSApiCallbackWithValue(callback, value, persist);
      });
  }
  void RemoveJSApiCallback(runtime::js::ApiCallBack callback) override {}
  void CallPlatformCallbackWithValue(
      const std::shared_ptr<shell::PlatformCallBackHolder>&,
      const lepus::Value& value) override {}
  void RemovePlatformCallback(
      const std::shared_ptr<shell::PlatformCallBackHolder>& callback) override {
  }
  void CallJSFunction(const std::string& module_id,
                      const std::string& method_id,
                      const lepus::Value& arguments) override {
    if (background)
      Post([this, module_id, method_id,
            args = lepus::Value::Clone(arguments).ToLepusValue(true)]() {
        background->CallJSFunction(module_id, method_id, args);
      });
  }
  void OnDataUpdatedByNative(tasm::TemplateData data,
                             const bool reset) override {}
  void OnJSAppReload(
      tasm::TemplateData data,
      const std::shared_ptr<PipelineOptions>& pipeline_options) override {}
  void OnGlobalPropsUpdated(const lepus::Value& props) override {}
  void OnLifecycleEvent(const lepus::Value& args) override {
    DispatchMessageEvent(fml::MakeRefCounted<runtime::MessageEvent>(
        runtime::kMessageEventTypeOnLifecycleEvent,
        runtime::ContextProxy::Type::kCoreContext,
        runtime::ContextProxy::Type::kJSContext,
        std::make_unique<pub::ValueImplLepus>(args)));
  }
  void OnI18nResourceChanged(const std::string& res) override {}
  void RequestVsync(
      uintptr_t id,
      base::MoveOnlyClosure<void, int64_t, int64_t> callback) override {
    clock->RequestAnimationFrame(id, std::move(callback));
  }
  lepus::Value TriggerLepusMethod(const std::string& method_name,
                                  const lepus::Value& arguments) override {
    return {};
  }
  void TriggerLepusMethodAsync(const std::string& method_name,
                               const lepus::Value& arguments,
                               bool is_air) override {}
  void InvokeUIMethod(LynxGetUIResult ui_result, const std::string& method,
                      fml::RefPtr<tasm::PropBundle> params,
                      runtime::js::ApiCallBack callback) override {}
  void LepusInvokeUIMethod(
      std::vector<int32_t> ui_impl_ids, const std::string& method,
      const lepus::Value& params, runtime::MTSRuntime* context,
      std::unique_ptr<lepus::Value> callback_closure) override {}
  void OnJSSourcePrepared(
      tasm::TasmRuntimeBundle bundle, const lepus::Value& global_props,
      const std::string& page_name, tasm::PackageInstanceDSL dsl,
      tasm::PackageInstanceBundleModuleMode bundle_module_mode,
      const std::string& url,
      const std::shared_ptr<PipelineOptions>& pipeline_options,
      uint64_t trace_flow_id) override {
    background_source_prepared = true;
    prepared = std::make_unique<Prepared>(Prepared{
        std::move(bundle), lepus::Value::Clone(global_props).ToLepusValue(true),
        page_name, dsl, bundle_module_mode, url, pipeline_options,
        trace_flow_id});
  }
  void OnComponentDecoded(tasm::TasmRuntimeBundle bundle) override {}
  void OnCardConfigDataChanged(const lepus::Value& data) override {}
  void OnRuntimeGC(
      std::unordered_map<std::string, std::string> mem_info) override {}
  fml::RefPtr<fml::TaskRunner> GetLepusTimedTaskRunner() override {
    return fml::MessageLoop::GetCurrent().GetTaskRunner();
  }
  void OnEventCapture(long target_id, bool is_catch,
                      int64_t event_id) override {}
  void OnEventBubble(long target_id, bool is_catch, int64_t event_id) override {
  }
  void OnEventFire(long target_id, bool is_stop, int64_t event_id) override {}
  void OnLynxEvent(const lepus::Value& event_detail) override {}
  void CreateLayoutNode(int32_t id, const base::String& tag) override {}
  void UpdateLayoutNodeFontSize(int32_t id, double cur_node_font_size,
                                double root_node_font_size,
                                double font_scale) override {}
  void InsertLayoutNode(int32_t parent_id, int32_t child_id,
                        int index) override {}
  void SendAnimationEvent(const std::string& type, int tag,
                          const lepus::Value& dict) override {}
  void SendNativeCustomEvent(const std::string& name, int tag,
                             const lepus::Value& param_value,
                             const std::string& param_name) override {}
  void InsertLayoutNodeBefore(int32_t parent_id, int32_t child_id,
                              int32_t ref_id) override {}
  void RemoveLayoutNode(int32_t parent_id, int32_t child_id) override {}
  void DestroyLayoutNode(int32_t id) override {}
  void UpdateLayoutNodeStyle(int32_t id, CSSPropertyID css_id,
                             const tasm::CSSValue& value) override {}
  void ResetLayoutNodeStyle(int32_t id, CSSPropertyID css_id) override {}
  void UpdateLayoutNodeAttribute(int32_t id, starlight::LayoutAttribute key,
                                 const lepus::Value& value) override {}
  void SetFontFaces(const CSSFontFaceRuleMap& fontfaces) override {}
  void UpdateLayoutNodeByBundle(int32_t id,
                                std::unique_ptr<LayoutBundle> bundle) override {
  }
  void UpdateLayoutNodeProps(int32_t id,
                             const fml::RefPtr<PropBundle>& props) override {}
  void MarkLayoutDirty(int32_t id) override {}
  void AttachLayoutNodeType(int32_t id, const base::String& tag,
                            bool allow_inline,
                            const fml::RefPtr<PropBundle>& props) override {}
  void UpdateLynxEnvForLayoutThread(LynxEnvConfig env) override {}
  void OnUpdateViewport(float width, int width_mode, float height,
                        int height_mode, bool need_layout) override {}
  void SetRootOnLayout(int32_t id) override {}
  void OnUpdateDataWithoutChange() override {}
  void SetPageConfigForLayoutThread(
      const std::shared_ptr<PageConfig>& config) override {}
  void ReportElementMemoryInfo(int64_t mem_size_bytes,
                               int element_count) override {}
  event::DispatchEventResult DispatchMessageEvent(
      fml::RefPtr<runtime::MessageEvent> message) override {
    auto payload =
        lepus::Value::Clone(
            pub::ValueUtils::ConvertValueToLepusValue(*message->message()))
            .ToLepusValue(true);
    message = fml::MakeRefCounted<runtime::MessageEvent>(
        message->type(), message->time_stamp(), message->GetOriginType(),
        message->GetTargetType(),
        std::make_unique<pub::ValueImplLepus>(std::move(payload)));

    if (message->IsSendingToJSThread()) {
      if (background)
        Post([this, message = std::move(message)]() mutable {
          background->OnReceiveMessageEvent(std::move(message));
        });
      else
        pending_messages.push_back(std::move(message));
    } else if (message->IsSendingToCoreThread()) {
      Post([this, message = std::move(message)]() mutable {
        if (owner) owner->OnReceiveMessageEvent(std::move(message));
      });
    }
    return {event::EventCancelType::kNotCanceled, true};
  }
  void InvokeResponsePromiseCallback(base::closure closure) override {
    closure();
  }
};
class CanvasPaintingContext : public PaintingContextPlatformImpl {
 public:
  explicit CanvasPaintingContext(std::vector<std::string>&) {}
  bool HasEnableUIOperationBatching() override { return true; }
  LayoutResult MeasureText(Element* element, float width, int width_mode,
                           float height, int height_mode) override;
  void DispatchLayoutBefore(Element*) override {}
  void AlignText(Element* element) override { texts.Align(element); }
  void DestroyText(Element* element) override { texts.Erase(element); }
  lynx::wasm::BrowserText texts;

};
class CanvasPropBundleCreator : public PropBundleCreator {
 public:
  fml::RefPtr<PropBundle> CreatePropBundle() override {
    return fml::AdoptRef<PropBundle>(new NativePropBundle());
  }
};
}  // namespace lynx::tasm
#endif  // PLATFORM_WASM_STATIC_BUNDLE_HOST_H_
