// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RUNTIME_LEPUSNG_NAPI_WORKLET_NAPI_EDIT_CONTEXT_H_
#define CORE_RUNTIME_LEPUSNG_NAPI_WORKLET_NAPI_EDIT_CONTEXT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/renderer/editing/editing_host_controller.h"
#include "third_party/binding/napi/napi_bridge.h"

namespace lynx::worklet {

// UI-thread N-API facade for the EditContext model. Element association is
// deliberately exposed as a small native API so the LepusElement binding can
// validate that its element is a live <text> before calling AssociateElement.
class NapiEditContext : public binding::NapiBridge {
 public:
  explicit NapiEditContext(const Napi::CallbackInfo& info);
  ~NapiEditContext() override;

  static void Install(Napi::Env env, Napi::Object target);
  static Napi::Function Constructor(Napi::Env env);
  static Napi::Class* Class(Napi::Env env);

  static bool IsInstance(const Napi::Value& value);
  static NapiEditContext* Unwrap(const Napi::Value& value);

  std::shared_ptr<editing::EditingHostController> controller() const {
    return controller_;
  }

  // The caller owns element validation. Host id, rather than wrapper identity,
  // enforces the one-element invariant because querySelector may create a new
  // JS wrapper for the same native element.
  bool AssociateElement(int64_t host_id, const Napi::Object& element);
  void DetachElement(int64_t host_id);
  void DetachElement();

  // Public native event entry point used by EditContextModel and association
  // glue. It must be called on the facade's UI/N-API thread.
  void DispatchEvent(const editing::EditContextEvent& event);

  Napi::Value GetText(const Napi::CallbackInfo& info);
  Napi::Value GetSelectionStart(const Napi::CallbackInfo& info);
  Napi::Value GetSelectionEnd(const Napi::CallbackInfo& info);
  Napi::Value AttachedElements(const Napi::CallbackInfo& info);
  Napi::Value UpdateText(const Napi::CallbackInfo& info);
  Napi::Value UpdateSelection(const Napi::CallbackInfo& info);
  Napi::Value AddEventListener(const Napi::CallbackInfo& info);
  Napi::Value RemoveEventListener(const Napi::CallbackInfo& info);

 private:
  static const char* EventTypeName(editing::EditContextEventType type);
  Napi::Object CreateEventObject(const editing::EditContextEvent& event);
  Napi::Object Self();

  std::shared_ptr<editing::EditingHostController> controller_;
  std::optional<int64_t> attached_host_id_;
  Napi::ObjectReference attached_element_;
  std::unordered_map<std::string, std::vector<Napi::FunctionReference>>
      event_listeners_;
};

}  // namespace lynx::worklet

#endif  // CORE_RUNTIME_LEPUSNG_NAPI_WORKLET_NAPI_EDIT_CONTEXT_H_
