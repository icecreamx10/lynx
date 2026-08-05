// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RUNTIME_LEPUSNG_NAPI_WORKLET_EDIT_CONTEXT_BINDING_REGISTRY_H_
#define CORE_RUNTIME_LEPUSNG_NAPI_WORKLET_EDIT_CONTEXT_BINDING_REGISTRY_H_

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "core/renderer/editing/editing_types.h"
#include "third_party/binding/napi/shim/shim_napi.h"

namespace lynx::editing {
class EditingHostController;
class EditingHostRegistry;
}  // namespace lynx::editing

namespace lynx::worklet {

// Owns the JavaScript identity associated with an editing host. The renderer's
// EditingHostRegistry remains the source of truth for controller attachment;
// this UI-runtime-scoped registry only adds the strong N-API reference needed
// for Element.editContext to return the exact object that was assigned.
class EditContextBindingRegistry {
 public:
  EditContextBindingRegistry(Napi::Env env,
                             editing::EditingHostRegistry* host_registry);
  ~EditContextBindingRegistry();

  EditContextBindingRegistry(const EditContextBindingRegistry&) = delete;
  EditContextBindingRegistry& operator=(const EditContextBindingRegistry&) =
      delete;

  bool Bind(int64_t host_id,
            std::shared_ptr<editing::EditingHostController> controller,
            const Napi::Object& object);
  void Unbind(int64_t host_id);

  // Returns null when the host has no binding or |env| is not this registry's
  // environment.
  Napi::Value GetObject(Napi::Env env, int64_t host_id) const;
  std::shared_ptr<editing::EditingHostController> GetController(
      int64_t host_id) const;

  editing::EditingHostRegistry* host_registry() const { return host_registry_; }

 private:
  struct Binding {
    std::shared_ptr<editing::EditingHostController> controller;
    Napi::ObjectReference object;
  };

  void OnHostLifecycle(int64_t host_id,
                       editing::EditingHostLifecycleEvent event);
  void ReleaseBinding(int64_t host_id);
  void Clear();

  void* env_{nullptr};
  editing::EditingHostRegistry* host_registry_{nullptr};
  uint64_t lifecycle_observer_id_{0};
  std::unordered_map<int64_t, Binding> bindings_;
};

}  // namespace lynx::worklet

#endif  // CORE_RUNTIME_LEPUSNG_NAPI_WORKLET_EDIT_CONTEXT_BINDING_REGISTRY_H_
