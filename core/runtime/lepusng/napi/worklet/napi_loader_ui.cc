// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/runtime/lepusng/napi/worklet/napi_loader_ui.h"

#include <memory>

#include "core/renderer/dom/element_manager.h"
#include "core/renderer/page_proxy.h"
#include "core/renderer/template_assembler.h"
#include "core/renderer/worklet/lepus_lynx.h"
#include "core/runtime/lepusng/napi/worklet/edit_context_binding_registry.h"
#include "core/runtime/lepusng/napi/worklet/napi_edit_context.h"
#include "core/runtime/lepusng/napi/worklet/napi_lepus_lynx.h"

#ifdef USE_PRIMJS_NAPI
#include "third_party/napi/include/primjs_napi_defines.h"
#endif

namespace lynx {
namespace worklet {

NapiLoaderUI::NapiLoaderUI(runtime::MTSRuntime* context) : context_(context) {}

NapiLoaderUI::~NapiLoaderUI() = default;

void NapiLoaderUI::OnAttach(Napi::Env env) {
  SetNapiEnvToLEPUSContext(env);
  NapiEnvToLoaderMap()[static_cast<napi_env>(env)] = this;

  auto* tasm = static_cast<tasm::TemplateAssembler*>(context_->GetDelegate());
  auto* host_registry =
      tasm && tasm->page_proxy() && tasm->page_proxy()->element_manager()
          ? tasm->page_proxy()->element_manager()->editing_host_registry()
          : nullptr;
  edit_context_binding_registry_ =
      std::make_unique<EditContextBindingRegistry>(env, host_registry);

  // Set Lynx To Napi Env
  lynx_ = LepusLynx::Create(env, context_->name(), tasm);
  constexpr const static char* kGlobalLynxName = "lepusLynx";
  Napi::HandleScope handle_scope(env);
  Napi::Object global = env.Global();
  NapiEditContext::Install(env, global);
  env.Global()[kGlobalLynxName] =
      NapiLepusLynx::Wrap(std::unique_ptr<LepusLynx>(lynx_), env);
}

void NapiLoaderUI::OnDetach(Napi::Env env) {
  auto use_env = static_cast<napi_env>(env);
  if (!use_env) {
    return;
  }
  edit_context_binding_registry_.reset();
  NapiEnvToLoaderMap().erase(use_env);
  auto& map = NapiEnvToContextMap();
  auto iter = map.find(use_env);
  if (iter == map.end()) {
    return;
  }
  auto* quick_context = iter->second;
  quick_context->set_napi_env(nullptr);
  map.erase(iter);

  lynx_ = nullptr;
}

void NapiLoaderUI::InvokeLepusBridge(const int32_t callback_id,
                                     const lepus::Value& data) {
  lynx_->InvokeLepusBridge(callback_id, data);
}

lepus::QuickContext* NapiLoaderUI::GetQuickContextFromNapiEnv(Napi::Env env) {
  auto& context_map = NapiLoaderUI::NapiEnvToContextMap();
  auto iter = context_map.find(static_cast<napi_env>(env));
  if (iter == context_map.end()) {
    return nullptr;
  }
  return iter->second;
}

NapiLoaderUI* NapiLoaderUI::GetLoaderFromNapiEnv(Napi::Env env) {
  auto& loader_map = NapiLoaderUI::NapiEnvToLoaderMap();
  auto iter = loader_map.find(static_cast<napi_env>(env));
  return iter == loader_map.end() ? nullptr : iter->second;
}

std::unordered_map<napi_env, lepus::QuickContext*>&
NapiLoaderUI::NapiEnvToContextMap() {
  static thread_local std::unordered_map<napi_env, lepus::QuickContext*> map;
  return map;
}

std::unordered_map<napi_env, NapiLoaderUI*>&
NapiLoaderUI::NapiEnvToLoaderMap() {
  static thread_local std::unordered_map<napi_env, NapiLoaderUI*> map;
  return map;
}

void NapiLoaderUI::SetNapiEnvToLEPUSContext(Napi::Env env) {
  auto quick_context = runtime::MTSRuntime::ToQuickContext(context_);
  if (quick_context == nullptr) {
    return;
  }
  quick_context->set_napi_env(
      reinterpret_cast<void*>(static_cast<napi_env>(env)));
  NapiEnvToContextMap()[static_cast<napi_env>(env)] = quick_context;
}

}  // namespace worklet
}  // namespace lynx

#ifdef USE_PRIMJS_NAPI
#include "third_party/napi/include/primjs_napi_undefs.h"
#endif
