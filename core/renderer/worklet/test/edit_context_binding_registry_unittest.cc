// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/runtime/lepusng/napi/worklet/edit_context_binding_registry.h"

#include <memory>

#include "core/renderer/editing/editing_host_controller.h"
#include "core/renderer/editing/editing_host_registry.h"
#include "core/runtime/common/napi/napi_environment.h"
#include "core/runtime/common/napi/napi_runtime_proxy.h"
#include "core/runtime/common/napi/napi_runtime_proxy_quickjs.h"
#include "core/runtime/common/napi/shim/shim_napi_env_quickjs.h"
#include "quickjs/include/quickjs.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx::worklet {
namespace {

class EmptyNapiDelegate final : public runtime::js::NapiEnvironment::Delegate {
};

class EditContextBindingRegistryTest : public ::testing::Test {
 protected:
  EditContextBindingRegistryTest()
      : runtime_(LEPUS_NewRuntime()),
        context_(LEPUS_NewContext(runtime_)),
        runtime_proxy_(runtime::js::NapiRuntimeProxyQuickjs::Create(context_)),
        env_(runtime_proxy_->Env()) {}

  void SetUp() override {
    napi_environment_ = std::make_unique<runtime::js::NapiEnvironment>(
        std::make_unique<EmptyNapiDelegate>());
    napi_environment_->SetRuntimeProxy(std::move(runtime_proxy_));
    napi_environment_->Attach();
  }

  ~EditContextBindingRegistryTest() override {
    napi_environment_.reset();
    LEPUS_FreeContext(context_);
    LEPUS_FreeRuntime(runtime_);
  }

  Napi::Env env() { return env_; }

  LEPUSRuntime* runtime_;
  LEPUSContext* context_;
  std::unique_ptr<runtime::js::NapiRuntimeProxy> runtime_proxy_;
  Napi::Env env_;
  std::unique_ptr<runtime::js::NapiEnvironment> napi_environment_;
};

TEST_F(EditContextBindingRegistryTest, PreservesObjectIdentityAndUniqueness) {
  editing::EditingHostRegistry host_registry;
  EditContextBindingRegistry registry(env(), &host_registry);
  auto controller = std::make_shared<editing::EditingHostController>(
      editing::EditContextOptions{});
  Napi::Object context_object = Napi::Object::New(env());

  ASSERT_TRUE(registry.Bind(11, controller, context_object));
  EXPECT_TRUE(registry.GetObject(env(), 11).StrictEquals(context_object));
  EXPECT_EQ(registry.GetController(11), controller);
  EXPECT_EQ(host_registry.Lookup(11), controller);

  Napi::Object second_element_object = Napi::Object::New(env());
  EXPECT_FALSE(registry.Bind(12, controller, second_element_object));
  EXPECT_TRUE(registry.GetObject(env(), 12).IsNull());
  EXPECT_EQ(host_registry.Lookup(12), nullptr);
}

TEST_F(EditContextBindingRegistryTest, TracksExternalDetachAndReplacement) {
  editing::EditingHostRegistry host_registry;
  EditContextBindingRegistry registry(env(), &host_registry);
  auto first = std::make_shared<editing::EditingHostController>(
      editing::EditContextOptions{});
  auto second = std::make_shared<editing::EditingHostController>(
      editing::EditContextOptions{});
  Napi::Object first_object = Napi::Object::New(env());
  Napi::Object second_object = Napi::Object::New(env());

  ASSERT_TRUE(registry.Bind(7, first, first_object));
  ASSERT_TRUE(registry.Bind(7, second, second_object));
  EXPECT_EQ(host_registry.Lookup(7), second);
  EXPECT_TRUE(registry.GetObject(env(), 7).StrictEquals(second_object));

  host_registry.Detach(7);
  EXPECT_TRUE(registry.GetObject(env(), 7).IsNull());
  EXPECT_EQ(registry.GetController(7), nullptr);
}

TEST_F(EditContextBindingRegistryTest, RuntimeTeardownDetachesOwnedHosts) {
  editing::EditingHostRegistry host_registry;
  auto controller = std::make_shared<editing::EditingHostController>(
      editing::EditContextOptions{});
  {
    EditContextBindingRegistry registry(env(), &host_registry);
    ASSERT_TRUE(registry.Bind(9, controller, Napi::Object::New(env())));
    ASSERT_EQ(host_registry.Lookup(9), controller);
  }
  EXPECT_EQ(host_registry.Lookup(9), nullptr);
}

}  // namespace
}  // namespace lynx::worklet
