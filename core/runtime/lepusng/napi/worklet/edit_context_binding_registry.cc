// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/runtime/lepusng/napi/worklet/edit_context_binding_registry.h"

#include <utility>
#include <vector>

#include "core/renderer/editing/editing_host_controller.h"
#include "core/renderer/editing/editing_host_registry.h"
#include "core/runtime/lepusng/napi/worklet/napi_edit_context.h"

namespace lynx::worklet {

EditContextBindingRegistry::EditContextBindingRegistry(
    Napi::Env env, editing::EditingHostRegistry* host_registry)
    : env_(static_cast<void*>(env)), host_registry_(host_registry) {
  if (host_registry_) {
    lifecycle_observer_id_ = host_registry_->AddLifecycleObserver(
        [this](int64_t host_id, editing::EditingHostLifecycleEvent event) {
          OnHostLifecycle(host_id, event);
        });
  }
}

EditContextBindingRegistry::~EditContextBindingRegistry() { Clear(); }

bool EditContextBindingRegistry::Bind(
    int64_t host_id, std::shared_ptr<editing::EditingHostController> controller,
    const Napi::Object& object) {
  if (!host_registry_ || !controller || object.IsEmpty() ||
      static_cast<void*>(object.Env()) != env_) {
    return false;
  }

  auto existing = bindings_.find(host_id);
  if (existing != bindings_.end() &&
      existing->second.controller == controller) {
    existing->second.object.Reset(object, 1);
    return true;
  }

  const auto controller_host = host_registry_->FindHost(controller);
  if (controller_host && *controller_host != host_id) {
    return false;
  }

  auto attached_controller = host_registry_->Lookup(host_id);
  if (attached_controller) {
    if (existing == bindings_.end() ||
        existing->second.controller != attached_controller) {
      return false;
    }
    host_registry_->Detach(host_id);
  } else {
    ReleaseBinding(host_id);
  }

  if (!host_registry_->Attach(host_id, controller)) {
    return false;
  }
  if (host_registry_->Lookup(host_id) != controller) {
    return false;
  }

  bindings_.emplace(host_id,
                    Binding{std::move(controller), Napi::Persistent(object)});
  return true;
}

void EditContextBindingRegistry::Unbind(int64_t host_id) {
  auto existing = bindings_.find(host_id);
  if (existing == bindings_.end()) {
    return;
  }
  if (host_registry_ &&
      host_registry_->Lookup(host_id) == existing->second.controller) {
    host_registry_->Detach(host_id);
  } else {
    ReleaseBinding(host_id);
  }
}

Napi::Value EditContextBindingRegistry::GetObject(Napi::Env env,
                                                  int64_t host_id) const {
  if (static_cast<void*>(env) != env_) {
    return env.Null();
  }
  auto it = bindings_.find(host_id);
  if (it == bindings_.end() || it->second.object.IsEmpty()) {
    return env.Null();
  }
  return it->second.object.Value();
}

std::shared_ptr<editing::EditingHostController>
EditContextBindingRegistry::GetController(int64_t host_id) const {
  auto it = bindings_.find(host_id);
  return it == bindings_.end() ? nullptr : it->second.controller;
}

void EditContextBindingRegistry::OnHostLifecycle(
    int64_t host_id, editing::EditingHostLifecycleEvent event) {
  if (event == editing::EditingHostLifecycleEvent::kDetached) {
    ReleaseBinding(host_id);
  }
}

void EditContextBindingRegistry::ReleaseBinding(int64_t host_id) {
  auto it = bindings_.find(host_id);
  if (it == bindings_.end()) {
    return;
  }
  Napi::Env env = it->second.object.Env();
  Napi::ContextScope context_scope(env);
  Napi::HandleScope handle_scope(env);
  if (auto* context = NapiEditContext::Unwrap(it->second.object.Value())) {
    context->DetachElement();
  }
  bindings_.erase(it);
}

void EditContextBindingRegistry::Clear() {
  if (!host_registry_) {
    bindings_.clear();
    return;
  }

  if (lifecycle_observer_id_ != 0) {
    host_registry_->RemoveLifecycleObserver(lifecycle_observer_id_);
    lifecycle_observer_id_ = 0;
  }

  std::vector<int64_t> host_ids;
  host_ids.reserve(bindings_.size());
  for (auto& [host_id, binding] : bindings_) {
    Napi::Env env = binding.object.Env();
    Napi::ContextScope context_scope(env);
    Napi::HandleScope handle_scope(env);
    if (auto* context = NapiEditContext::Unwrap(binding.object.Value())) {
      context->DetachElement();
    }
    if (host_registry_->Lookup(host_id) == binding.controller) {
      host_ids.push_back(host_id);
    }
  }
  for (int64_t host_id : host_ids) {
    host_registry_->Detach(host_id);
  }
  bindings_.clear();
  host_registry_ = nullptr;
}

}  // namespace lynx::worklet
