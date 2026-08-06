// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/runtime/lepusng/napi/worklet/napi_loader_ui.h"

#include <cmath>
#include <limits>
#include <memory>

#include "core/renderer/dom/element_manager.h"
#include "core/renderer/page_proxy.h"
#include "core/renderer/template_assembler.h"
#include "core/renderer/worklet/lepus_element.h"
#include "core/renderer/worklet/lepus_lynx.h"
#include "core/renderer/worklet/lepus_raf_handler.h"
#include "core/runtime/lepusng/napi/worklet/edit_context_binding_registry.h"
#include "core/runtime/lepusng/napi/worklet/napi_edit_context.h"
#include "core/runtime/lepusng/napi/worklet/napi_lepus_element.h"
#include "core/runtime/lepusng/napi/worklet/napi_lepus_lynx.h"

#ifdef USE_PRIMJS_NAPI
#include "third_party/napi/include/primjs_napi_defines.h"
#endif

namespace lynx {
namespace worklet {

namespace {

struct FrontendElementBridgeState {
  Napi::ObjectReference native_wrapper;

  LepusElement* Bridge() {
    if (native_wrapper.IsEmpty()) {
      return nullptr;
    }
    auto* wrapper =
        Napi::ObjectWrap<NapiLepusElement>::Unwrap(native_wrapper.Value());
    return wrapper ? wrapper->ToImplUnsafe() : nullptr;
  }
};

bool ReadFrontendHostId(const Napi::CallbackInfo& info, int32_t* host_id) {
  if (info.Length() < 1 || !info[0].IsNumber()) {
    return false;
  }
  const double number = info[0].As<Napi::Number>().DoubleValue();
  if (number < 0 || number > std::numeric_limits<int32_t>::max() ||
      std::floor(number) != number) {
    return false;
  }
  *host_id = static_cast<int32_t>(number);
  return true;
}

Napi::Value DispatchEditContextFrontendBridge(
    NapiEditContext* context,
    NapiEditContext::FrontendBridgeOperation operation,
    const Napi::CallbackInfo& info) {
  auto* loader = NapiLoaderUI::GetLoaderFromNapiEnv(info.Env());
  auto* registry = loader ? loader->edit_context_binding_registry() : nullptr;
  if (operation == NapiEditContext::FrontendBridgeOperation::kGet) {
    int32_t host_id = -1;
    return registry && ReadFrontendHostId(info, &host_id)
               ? registry->GetObject(info.Env(), host_id)
               : info.Env().Null();
  }
  if (!context || !loader || !registry) {
    Napi::Error::New(info.Env(), "EditContext frontend bridge is unavailable")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }

  if (operation == NapiEditContext::FrontendBridgeOperation::kFocus ||
      operation == NapiEditContext::FrontendBridgeOperation::kBlur) {
    auto state = std::static_pointer_cast<FrontendElementBridgeState>(
        context->frontend_bridge_state());
    auto* bridge = state ? state->Bridge() : nullptr;
    if (bridge) {
      operation == NapiEditContext::FrontendBridgeOperation::kFocus
          ? bridge->Focus()
          : bridge->Blur();
    }
    return info.Env().Undefined();
  }

  int32_t host_id = -1;
  if (!ReadFrontendHostId(info, &host_id)) {
    Napi::TypeError::New(info.Env(),
                         "EditContext frontend bridge requires a host id")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  if (operation == NapiEditContext::FrontendBridgeOperation::kDetach) {
    if (context->attached_host_id() &&
        *context->attached_host_id() == host_id) {
      registry->Unbind(host_id);
    }
    return info.Env().Undefined();
  }
  if (info.Length() < 2 || !info[1].IsObject() || info[1].IsNull()) {
    Napi::TypeError::New(
        info.Env(),
        "EditContext frontend attachment requires an element object")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  if (context->attached_host_id() && *context->attached_host_id() != host_id) {
    Napi::TypeError::New(
        info.Env(), "An EditContext cannot be associated with two elements")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }

  auto state = std::static_pointer_cast<FrontendElementBridgeState>(
      context->frontend_bridge_state());
  if (!state) {
    auto* tasm = loader->template_assembler();
    if (!tasm || !loader->element_task_handler()) {
      Napi::Error::New(info.Env(),
                       "EditContext frontend element is unavailable")
          .ThrowAsJavaScriptException();
      return info.Env().Undefined();
    }
    state = std::make_shared<FrontendElementBridgeState>();
    auto bridge = std::unique_ptr<LepusElement>(
        LepusElement::Create(host_id, tasm, loader->element_task_handler()));
    state->native_wrapper =
        Napi::Persistent(NapiLepusElement::Wrap(std::move(bridge), info.Env()));
  }
  auto* bridge = state->Bridge();
  if (!bridge) {
    Napi::Error::New(info.Env(), "EditContext frontend element is unavailable")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  bridge->SetEditContext(context->ObjectForFrontend(),
                         info[1].As<Napi::Object>());
  if (!info.Env().IsExceptionPending() && context->attached_host_id() &&
      *context->attached_host_id() == host_id) {
    context->SetFrontendBridgeState(std::move(state));
  }
  return info.Env().Undefined();
}

}  // namespace

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
  element_task_handler_ = std::make_shared<LepusApiHandler>();
  NapiEditContext::SetFrontendBridgeDispatcher(
      &DispatchEditContextFrontendBridge);

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
  element_task_handler_.reset();
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

tasm::TemplateAssembler* NapiLoaderUI::template_assembler() const {
  return context_
             ? static_cast<tasm::TemplateAssembler*>(context_->GetDelegate())
             : nullptr;
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
