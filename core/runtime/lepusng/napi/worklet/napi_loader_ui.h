// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RUNTIME_LEPUSNG_NAPI_WORKLET_NAPI_LOADER_UI_H_
#define CORE_RUNTIME_LEPUSNG_NAPI_WORKLET_NAPI_LOADER_UI_H_

#include <memory>
#include <unordered_map>

#include "core/runtime/common/napi/napi_environment.h"
#include "core/runtime/lepusng/quick_context.h"
#include "third_party/binding/napi/shim/shim_napi.h"

#ifdef USE_PRIMJS_NAPI
#include "third_party/napi/include/primjs_napi_defines.h"
#endif

namespace lynx {
namespace tasm {
class TemplateAssembler;
}
namespace worklet {

class EditContextBindingRegistry;
class LepusApiHandler;
class LepusLynx;

class NapiLoaderUI : public runtime::js::NapiEnvironment::Delegate {
 public:
  NapiLoaderUI(runtime::MTSRuntime* context);
  ~NapiLoaderUI() override;

  void OnAttach(Napi::Env env) override;
  void OnDetach(Napi::Env env) override;
  lynx::worklet::LepusLynx* lepus_lynx() { return lynx_; }
  EditContextBindingRegistry* edit_context_binding_registry() {
    return edit_context_binding_registry_.get();
  }
  tasm::TemplateAssembler* template_assembler() const;
  const std::shared_ptr<LepusApiHandler>& element_task_handler() const {
    return element_task_handler_;
  }
  void InvokeLepusBridge(const int32_t callback_id, const lepus::Value& data);

  static lepus::QuickContext* GetQuickContextFromNapiEnv(Napi::Env env);
  static NapiLoaderUI* GetLoaderFromNapiEnv(Napi::Env env);

 private:
  static std::unordered_map<napi_env, lepus::QuickContext*>&
  NapiEnvToContextMap();
  static std::unordered_map<napi_env, NapiLoaderUI*>& NapiEnvToLoaderMap();
  void SetNapiEnvToLEPUSContext(Napi::Env env);

  lynx::worklet::LepusLynx* lynx_ = nullptr;
  std::unique_ptr<EditContextBindingRegistry> edit_context_binding_registry_;
  std::shared_ptr<LepusApiHandler> element_task_handler_;
  runtime::MTSRuntime* context_ = nullptr;
};

}  // namespace worklet
}  // namespace lynx

#ifdef USE_PRIMJS_NAPI
#include "third_party/napi/include/primjs_napi_undefs.h"
#endif

#endif  // CORE_RUNTIME_LEPUSNG_NAPI_WORKLET_NAPI_LOADER_UI_H_
