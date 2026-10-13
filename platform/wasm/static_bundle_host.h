// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_STATIC_BUNDLE_HOST_H_
#define PLATFORM_WASM_STATIC_BUNDLE_HOST_H_
#include <cstdio>

#include "base/include/fml/message_loop.h"
#include "core/renderer/dom/lynx_get_ui_result.h"
#include "core/renderer/template_assembler.h"
#include "core/renderer/ui_wrapper/common/native_prop_bundle.h"
#include "core/renderer/ui_wrapper/painting/empty/painting_context_implementation.h"
namespace lynx::tasm {
// Static first-paint host. Native modules, background JS, interaction and
// asynchronous resources are deliberately not implemented by this adapter.
class StaticBundleDelegate : public TemplateAssembler::Delegate,
                             public ElementManager::Delegate {
 public:
  std::unordered_map<int32_t, LayoutInfoArray> GetSubTreeLayoutInfo(
      int32_t, Viewport = Viewport{}) override {
    return {};
  }
  bool loaded = false;
  bool background_source_prepared = false;
  std::vector<std::string> errors;
  void OnDataUpdated() override {}
  void OnTasmFinishByNative() override {}
  void OnRunPipelineFinished() override {}
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
  void OnPageUpdated(bool is_first_screen) override {}
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
  void CallJSApiCallback(runtime::js::ApiCallBack callback) override {}
  void CallJSApiCallbackWithValue(runtime::js::ApiCallBack callback,
                                  const lepus::Value& value,
                                  bool persist = false) override {}
  void RemoveJSApiCallback(runtime::js::ApiCallBack callback) override {}
  void CallPlatformCallbackWithValue(
      const std::shared_ptr<shell::PlatformCallBackHolder>&,
      const lepus::Value& value) override {}
  void RemovePlatformCallback(
      const std::shared_ptr<shell::PlatformCallBackHolder>& callback) override {
  }
  void CallJSFunction(const std::string& module_id,
                      const std::string& method_id,
                      const lepus::Value& arguments) override {}
  void OnDataUpdatedByNative(tasm::TemplateData data,
                             const bool reset) override {}
  void OnJSAppReload(
      tasm::TemplateData data,
      const std::shared_ptr<PipelineOptions>& pipeline_options) override {}
  void OnGlobalPropsUpdated(const lepus::Value& props) override {}
  void OnLifecycleEvent(const lepus::Value& args) override {}
  void OnI18nResourceChanged(const std::string& res) override {}
  void RequestVsync(
      uintptr_t id,
      base::MoveOnlyClosure<void, int64_t, int64_t> callback) override {}
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
      fml::RefPtr<runtime::MessageEvent>) override {
    return {};
  }
  void InvokeResponsePromiseCallback(base::closure closure) override {
    closure();
  }
};
class CanvasPaintingContext : public PaintingContextPlatformImpl {
 public:
  explicit CanvasPaintingContext(std::vector<std::string>& errors)
      : errors_(errors) {}
  bool HasEnableUIOperationBatching() override { return true; }
  LayoutResult MeasureText(Element* element, float width, int width_mode,
                           float height, int height_mode) override;
  // This prototype uses a single-line text adapter, with no separate shaping
  // or alignment pass. ElementManager still owns CSS and Starlight layout.
  void DispatchLayoutBefore(Element*) override {}
  void AlignText(Element*) override {}
  void DestroyText(Element*) override {}

 private:
  std::vector<std::string>& errors_;
};
class CanvasPropBundleCreator : public PropBundleCreator {
 public:
  fml::RefPtr<PropBundle> CreatePropBundle() override {
    return fml::AdoptRef<PropBundle>(new NativePropBundle());
  }
};
}  // namespace lynx::tasm
#endif  // PLATFORM_WASM_STATIC_BUNDLE_HOST_H_
