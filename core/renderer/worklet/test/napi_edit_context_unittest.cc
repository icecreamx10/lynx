// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/runtime/lepusng/napi/worklet/napi_edit_context.h"

#include <memory>

#include "core/runtime/common/napi/napi_environment.h"
#include "core/runtime/common/napi/napi_runtime_proxy.h"
#include "core/runtime/common/napi/napi_runtime_proxy_quickjs.h"
#include "core/runtime/common/napi/shim/shim_napi_env_quickjs.h"
#include "quickjs/include/quickjs.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx::worklet {
namespace {

class EditContextTestDelegate final
    : public runtime::js::NapiEnvironment::Delegate {
 public:
  void OnAttach(Napi::Env env) override {
    Napi::Object global = env.Global();
    NapiEditContext::Install(env, global);
  }
};

class NapiEditContextTest : public ::testing::Test {
 public:
  NapiEditContextTest()
      : runtime_(LEPUS_NewRuntime()),
        context_(LEPUS_NewContext(runtime_)),
        runtime_proxy_(runtime::js::NapiRuntimeProxyQuickjs::Create(context_)),
        env_(runtime_proxy_->Env()) {}

  void SetUp() override {
    environment_ = std::make_unique<runtime::js::NapiEnvironment>(
        std::make_unique<EditContextTestDelegate>());
    environment_->SetRuntimeProxy(std::move(runtime_proxy_));
    environment_->Attach();
  }

  ~NapiEditContextTest() override {
    environment_.reset();
    LEPUS_FreeContext(context_);
    LEPUS_FreeRuntime(runtime_);
  }

 protected:
  void ExpectScriptTrue(const char* source) {
    Napi::HandleScope scope(env_);
    Napi::Value result = env_.RunScript(source);
    ASSERT_FALSE(result.IsEmpty());
    EXPECT_TRUE(result.ToBoolean().Value());
  }

  LEPUSRuntime* runtime_;
  LEPUSContext* context_;
  std::unique_ptr<runtime::js::NapiRuntimeProxy> runtime_proxy_;
  Napi::Env env_;
  std::unique_ptr<runtime::js::NapiEnvironment> environment_;
};

TEST_F(NapiEditContextTest, ConstructorAndMutatorsUseUtf16Offsets) {
  ExpectScriptTrue(R"(
    globalThis.context = new EditContext({
      text: 'a\ud83d\ude00z', selectionStart: 1, selectionEnd: 3
    });
    context.text === 'a\ud83d\ude00z' &&
      context.selectionStart === 1 && context.selectionEnd === 3 &&
      (context.updateText(1, 3, 'X'), context.text === 'aXz') &&
      (context.updateSelection(2, 1), context.selectionStart === 2 &&
       context.selectionEnd === 1)
  )");

  ExpectScriptTrue(R"(
    (() => {
      try { context.updateText(8, 9, 'x'); } catch (error) {
        return error instanceof RangeError &&
          error.message === 'Invalid EditContext text range';
      }
      return false;
    })()
  )");
}

TEST_F(NapiEditContextTest, NativeModelDispatchesSampleEventShape) {
  ExpectScriptTrue(R"(
    globalThis.events = [];
    globalThis.context = new EditContext({
      text: 'abc', selectionStart: 1, selectionEnd: 1
    });
    context.addEventListener('compositionstart',
      event => events.push(event.type));
    context.addEventListener('textupdate', event => events.push([
      event.type, event.updateRangeStart, event.updateRangeEnd, event.text,
      event.selectionStart, event.selectionEnd
    ].join(':')));
    context.addEventListener('compositionend',
      event => events.push(event.type));
    true
  )");

  Napi::HandleScope scope(env_);
  NapiEditContext* facade =
      NapiEditContext::Unwrap(env_.Global().Get("context"));
  ASSERT_NE(facade, nullptr);
  auto& model = facade->controller()->edit_context();
  EXPECT_TRUE(model.ApplyNativeTextUpdate(1, 2, u"XY", editing::TextRange(3),
                                          editing::TextRange(1, 3)));
  EXPECT_TRUE(model.ApplyNativeTextUpdate(1, 3, u"Q", editing::TextRange(2),
                                          std::nullopt));

  ExpectScriptTrue(R"(
    events.length === 4 &&
      events[0] === 'compositionstart' &&
      events[1] === 'textupdate:1:2:XY:3:3' &&
      events[2] === 'textupdate:1:3:Q:2:2' &&
      events[3] === 'compositionend'
  )");
}

TEST_F(NapiEditContextTest, AssociationIsOneToOneAndPreservesIdentity) {
  Napi::HandleScope scope(env_);
  Napi::Object context = NapiEditContext::Constructor(env_).New(
      {env_.RunScript("({text: 'abc', selectionStart: 0, selectionEnd: 0})")});
  env_.Global().Set("context", context);
  NapiEditContext* facade = NapiEditContext::Unwrap(context);
  ASSERT_NE(facade, nullptr);

  Napi::Object first = Napi::Object::New(env_);
  Napi::Object second = Napi::Object::New(env_);
  EXPECT_TRUE(facade->AssociateElement(42, first));
  EXPECT_TRUE(facade->AssociateElement(42, second));
  EXPECT_FALSE(facade->AssociateElement(43, first));
  EXPECT_TRUE(facade->controller()->attached());

  Napi::Array attached = context.Get("attachedElements").As<Napi::Array>();
  EXPECT_EQ(attached.Length(), 1u);
  EXPECT_TRUE(attached.Get(0u).StrictEquals(second));

  facade->DetachElement(43);
  EXPECT_TRUE(facade->controller()->attached());
  facade->DetachElement(42);
  EXPECT_FALSE(facade->controller()->attached());
}

}  // namespace
}  // namespace lynx::worklet
