// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <memory>

#include "core/renderer/editing/editing_host_controller.h"
#include "core/renderer/editing/editing_host_registry.h"
#include "core/renderer/lynx_env_config.h"
#include "core/renderer/tasm/react/testing/mock_painting_context.h"
#include "core/shell/testing/mock_tasm_delegate.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx::worklet {
namespace {

TEST(EditContextElementLifecycleTest, ElementDestructionDetachesHost) {
  tasm::LynxEnvConfig env_config(100, 100, 1.f, 1.0);
  auto delegate =
      std::make_unique<::testing::NiceMock<tasm::test::MockTasmDelegate>>();
  auto manager = std::make_unique<tasm::ElementManager>(
      std::make_unique<tasm::MockPaintingContext>(), delegate.get(),
      env_config);
  auto controller = std::make_shared<editing::EditingHostController>(
      editing::EditContextOptions{});
  auto element = manager->CreateFiberElement("text");
  const int64_t host_id = element->impl_id();

  ASSERT_TRUE(manager->editing_host_registry()->Attach(host_id, controller));
  element = nullptr;
  EXPECT_EQ(manager->editing_host_registry()->Lookup(host_id), nullptr);
}

}  // namespace
}  // namespace lynx::worklet
