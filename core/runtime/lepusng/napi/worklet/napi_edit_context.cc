// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/runtime/lepusng/napi/worklet/napi_edit_context.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "base/include/vector.h"
#include "third_party/binding/napi/napi_base_wrap.h"

#ifdef USE_PRIMJS_NAPI
#include "third_party/napi/include/primjs_napi_defines.h"
#endif

namespace lynx::worklet {
namespace {

const uint64_t kEditContextClassID =
    reinterpret_cast<uint64_t>(&kEditContextClassID);
const uint64_t kEditContextConstructorID =
    reinterpret_cast<uint64_t>(&kEditContextConstructorID);

using Wrapped = binding::NapiBaseWrapped<NapiEditContext>;
using InstanceCallback =
    Napi::Value (NapiEditContext::*)(const Napi::CallbackInfo& info);

void AddAccessor(base::Vector<Wrapped::PropertyDescriptor>& properties,
                 const char* name, InstanceCallback getter) {
  properties.push_back(Wrapped::InstanceAccessor(name, getter, nullptr,
                                                 napi_default_jsproperty));
}

void AddMethod(base::Vector<Wrapped::PropertyDescriptor>& properties,
               const char* name, InstanceCallback method) {
  properties.push_back(
      Wrapped::InstanceMethod(name, method, napi_default_jsproperty));
}

bool ReadIndex(Napi::Env env, const Napi::Value& value, size_t* result) {
  if (!value.IsNumber()) {
    return false;
  }
  const double number = value.As<Napi::Number>().DoubleValue();
  if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
      number > static_cast<double>(std::numeric_limits<size_t>::max())) {
    return false;
  }
  *result = static_cast<size_t>(number);
  return true;
}

void ThrowTypeError(Napi::Env env, const char* message) {
  Napi::TypeError::New(env, message).ThrowAsJavaScriptException();
}

void ThrowRangeError(Napi::Env env, const char* message) {
  Napi::RangeError::New(env, message).ThrowAsJavaScriptException();
}

}  // namespace

NapiEditContext::NapiEditContext(const Napi::CallbackInfo& info)
    : NapiBridge(info) {
  set_type_id(reinterpret_cast<void*>(kEditContextClassID));
  if (info.Length() < 1 || !info[0].IsObject() || info[0].IsNull()) {
    ThrowTypeError(info.Env(), "EditContext options must be an object");
    return;
  }

  Napi::Object options = info[0].As<Napi::Object>();
  editing::EditContextOptions native_options;

  Napi::Value text = options.Get("text");
  if (text.IsEmpty()) {
    return;
  }
  if (!text.IsUndefined()) {
    if (!text.IsString()) {
      ThrowTypeError(info.Env(), "EditContext text must be a string");
      return;
    }
    native_options.text = text.As<Napi::String>().Utf16Value();
  }

  Napi::Value selection_start = options.Get("selectionStart");
  if (selection_start.IsEmpty()) {
    return;
  }
  if (!selection_start.IsUndefined() &&
      !ReadIndex(info.Env(), selection_start,
                 &native_options.selection_start)) {
    ThrowRangeError(info.Env(), "Invalid EditContext selection");
    return;
  }

  Napi::Value selection_end = options.Get("selectionEnd");
  if (selection_end.IsEmpty()) {
    return;
  }
  if (!selection_end.IsUndefined() &&
      !ReadIndex(info.Env(), selection_end, &native_options.selection_end)) {
    ThrowRangeError(info.Env(), "Invalid EditContext selection");
    return;
  }
  if (native_options.selection_start > native_options.text.size() ||
      native_options.selection_end > native_options.text.size()) {
    ThrowRangeError(info.Env(), "Invalid EditContext selection");
    return;
  }

  controller_ = std::make_shared<editing::EditingHostController>(
      std::move(native_options));
  controller_->edit_context().SetEventCallback(
      [this](const editing::EditContextEvent& event) { DispatchEvent(event); });
}

NapiEditContext::~NapiEditContext() {
  if (controller_) {
    controller_->edit_context().SetEventCallback(nullptr);
    controller_->Detach();
  }
}

bool NapiEditContext::IsInstance(const Napi::Value& value) {
  if (!value.IsObject()) {
    return false;
  }
  return value.As<Napi::Object>()
      .InstanceOf(Constructor(value.Env()))
      .FromMaybe(false);
}

NapiEditContext* NapiEditContext::Unwrap(const Napi::Value& value) {
  if (!IsInstance(value)) {
    return nullptr;
  }
  return Napi::InstanceWrap<NapiEditContext>::Unwrap(value.As<Napi::Object>());
}

bool NapiEditContext::AssociateElement(int64_t host_id,
                                       const Napi::Object& element) {
  if (!controller_ || element.IsEmpty()) {
    return false;
  }
  if (attached_host_id_ && *attached_host_id_ != host_id) {
    return false;
  }
  // Keep the exact assigned wrapper alive until explicit host detachment. The
  // association layer must call DetachElement on element teardown to break the
  // reference cycle when the element also retains this EditContext.
  attached_host_id_ = host_id;
  attached_element_.Reset(element, 1);
  controller_->Attach();
  return true;
}

void NapiEditContext::DetachElement(int64_t host_id) {
  if (!attached_host_id_ || *attached_host_id_ != host_id) {
    return;
  }
  DetachElement();
}

void NapiEditContext::DetachElement() {
  attached_host_id_.reset();
  attached_element_.Reset();
  if (controller_) {
    controller_->Detach();
  }
}

Napi::Value NapiEditContext::GetText(const Napi::CallbackInfo& info) {
  return Napi::String::New(info.Env(), controller_->edit_context().text());
}

Napi::Value NapiEditContext::GetSelectionStart(const Napi::CallbackInfo& info) {
  return Napi::Number::New(info.Env(),
                           controller_->edit_context().selection().base());
}

Napi::Value NapiEditContext::GetSelectionEnd(const Napi::CallbackInfo& info) {
  return Napi::Number::New(info.Env(),
                           controller_->edit_context().selection().extent());
}

Napi::Value NapiEditContext::AttachedElements(const Napi::CallbackInfo& info) {
  Napi::Array result = Napi::Array::New(info.Env());
  if (attached_element_.IsEmpty()) {
    return result;
  }
  Napi::Object attached = attached_element_.Value();
  if (attached.IsEmpty()) {
    DetachElement();
    return result;
  }
  result.Set(0u, attached);
  return result;
}

Napi::Value NapiEditContext::UpdateText(const Napi::CallbackInfo& info) {
  if (info.Length() < 3 || !info[2].IsString()) {
    ThrowTypeError(info.Env(),
                   "EditContext.updateText requires start, end, and text");
    return info.Env().Undefined();
  }
  size_t start = 0;
  size_t end = 0;
  if (!ReadIndex(info.Env(), info[0], &start) ||
      !ReadIndex(info.Env(), info[1], &end) ||
      !controller_->UpdateText(start, end,
                               info[2].As<Napi::String>().Utf16Value())) {
    ThrowRangeError(info.Env(), "Invalid EditContext text range");
  }
  return info.Env().Undefined();
}

Napi::Value NapiEditContext::UpdateSelection(const Napi::CallbackInfo& info) {
  size_t start = 0;
  size_t end = 0;
  if (info.Length() < 2 || !ReadIndex(info.Env(), info[0], &start) ||
      !ReadIndex(info.Env(), info[1], &end) ||
      !controller_->UpdateSelection(start, end)) {
    ThrowRangeError(info.Env(), "Invalid EditContext selection");
  }
  return info.Env().Undefined();
}

Napi::Value NapiEditContext::AddEventListener(const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsFunction()) {
    ThrowTypeError(info.Env(),
                   "EditContext.addEventListener requires a type and listener");
    return info.Env().Undefined();
  }
  const std::string type = info[0].As<Napi::String>().Utf8Value();
  Napi::Function listener = info[1].As<Napi::Function>();
  auto& listeners = event_listeners_[type];
  const bool duplicate = std::any_of(
      listeners.begin(), listeners.end(),
      [&](const auto& item) { return item.Value().StrictEquals(listener); });
  if (!duplicate) {
    listeners.push_back(Napi::Persistent(listener));
  }
  return info.Env().Undefined();
}

Napi::Value NapiEditContext::RemoveEventListener(
    const Napi::CallbackInfo& info) {
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsFunction()) {
    ThrowTypeError(
        info.Env(),
        "EditContext.removeEventListener requires a type and listener");
    return info.Env().Undefined();
  }
  const std::string type = info[0].As<Napi::String>().Utf8Value();
  auto found = event_listeners_.find(type);
  if (found == event_listeners_.end()) {
    return info.Env().Undefined();
  }
  Napi::Function listener = info[1].As<Napi::Function>();
  auto& listeners = found->second;
  listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                 [&](const auto& item) {
                                   return item.Value().StrictEquals(listener);
                                 }),
                  listeners.end());
  if (listeners.empty()) {
    event_listeners_.erase(found);
  }
  return info.Env().Undefined();
}

const char* NapiEditContext::EventTypeName(editing::EditContextEventType type) {
  switch (type) {
    case editing::EditContextEventType::kTextUpdate:
      return "textupdate";
    case editing::EditContextEventType::kTextFormatUpdate:
      return "textformatupdate";
    case editing::EditContextEventType::kCharacterBoundsUpdate:
      return "characterboundsupdate";
    case editing::EditContextEventType::kCompositionStart:
      return "compositionstart";
    case editing::EditContextEventType::kCompositionEnd:
      return "compositionend";
  }
  return "";
}

Napi::Object NapiEditContext::CreateEventObject(
    const editing::EditContextEvent& event) {
  Napi::Env env = Env();
  Napi::Object result = Napi::Object::New(env);
  result.Set("type", EventTypeName(event.type));
  if (event.type == editing::EditContextEventType::kTextUpdate) {
    result.Set("updateRangeStart",
               Napi::Number::New(env, event.update_range.start()));
    result.Set("updateRangeEnd",
               Napi::Number::New(env, event.update_range.end()));
    result.Set("text", Napi::String::New(env, event.text));
    result.Set("selectionStart",
               Napi::Number::New(env, event.selection.base()));
    result.Set("selectionEnd",
               Napi::Number::New(env, event.selection.extent()));
  }
  return result;
}

Napi::Object NapiEditContext::Self() { return NapiObject(); }

void NapiEditContext::DispatchEvent(const editing::EditContextEvent& event) {
  const char* type = EventTypeName(event.type);
  auto found = event_listeners_.find(type);
  if (found == event_listeners_.end()) {
    return;
  }

  Napi::Env env = Env();
  Napi::ContextScope context_scope(env);
  Napi::HandleScope handle_scope(env);
  Napi::Object event_object = CreateEventObject(event);
  Napi::Object self = Self();

  // Work on a handle snapshot. A listener may remove itself while running.
  std::vector<Napi::Function> listeners;
  listeners.reserve(found->second.size());
  for (const auto& listener : found->second) {
    listeners.push_back(listener.Value());
  }
  for (const auto& listener : listeners) {
    Napi::Value result = listener.Call(self, {event_object});
    if (result.IsEmpty() || env.IsExceptionPending()) {
      return;
    }
  }
}

Napi::Class* NapiEditContext::Class(Napi::Env env) {
  auto* clazz = env.GetInstanceData<Napi::Class>(kEditContextClassID);
  if (clazz) {
    return clazz;
  }

  base::InlineVector<Wrapped::PropertyDescriptor, 10> properties;
  AddAccessor(properties, "text", &NapiEditContext::GetText);
  AddAccessor(properties, "selectionStart",
              &NapiEditContext::GetSelectionStart);
  AddAccessor(properties, "selectionEnd", &NapiEditContext::GetSelectionEnd);
  AddAccessor(properties, "attachedElements",
              &NapiEditContext::AttachedElements);
  AddMethod(properties, "updateText", &NapiEditContext::UpdateText);
  AddMethod(properties, "updateSelection", &NapiEditContext::UpdateSelection);
  AddMethod(properties, "addEventListener", &NapiEditContext::AddEventListener);
  AddMethod(properties, "removeEventListener",
            &NapiEditContext::RemoveEventListener);

  clazz = new Napi::Class(
      Wrapped::DefineClass(env, "EditContext", properties.size(),
                           properties.data<const napi_property_descriptor>()));
  env.SetInstanceData<Napi::Class>(kEditContextClassID, clazz);
  return clazz;
}

Napi::Function NapiEditContext::Constructor(Napi::Env env) {
  auto* reference =
      env.GetInstanceData<Napi::FunctionReference>(kEditContextConstructorID);
  if (reference) {
    return reference->Value();
  }
  reference = new Napi::FunctionReference();
  reference->Reset(Class(env)->Get(env), 1);
  env.SetInstanceData<Napi::FunctionReference>(kEditContextConstructorID,
                                               reference);
  return reference->Value();
}

void NapiEditContext::Install(Napi::Env env, Napi::Object target) {
  if (!target.Has("EditContext").FromMaybe(false)) {
    target.Set("EditContext", Constructor(env));
  }
}

}  // namespace lynx::worklet

#ifdef USE_PRIMJS_NAPI
#include "third_party/napi/include/primjs_napi_undefs.h"
#endif
