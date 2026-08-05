// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/editing_host_registry.h"

#include <memory>
#include <utility>
#include <vector>

#include "core/renderer/editing/editing_host_controller.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx::editing {
namespace {

TEST(EditingHostRegistryTest, PublishesSingleHostLifecycleInOrder) {
  EditingHostRegistry registry;
  auto first = std::make_shared<EditingHostController>(EditContextOptions{});
  auto second = std::make_shared<EditingHostController>(EditContextOptions{});
  std::vector<std::pair<int64_t, EditingHostLifecycleEvent>> events;
  registry.AddLifecycleObserver(
      [&events](int64_t host_id, EditingHostLifecycleEvent event) {
        events.emplace_back(host_id, event);
      });

  ASSERT_TRUE(registry.Attach(10, first));
  ASSERT_TRUE(registry.Attach(20, second));
  ASSERT_TRUE(registry.Activate(10));
  ASSERT_TRUE(registry.Activate(20));

  registry.Detach(20);
  EXPECT_FALSE(registry.active_host_id().has_value());
  EXPECT_EQ(events,
            (std::vector<std::pair<int64_t, EditingHostLifecycleEvent>>{
                {10, EditingHostLifecycleEvent::kAttached},
                {20, EditingHostLifecycleEvent::kAttached},
                {10, EditingHostLifecycleEvent::kActivated},
                {10, EditingHostLifecycleEvent::kDeactivated},
                {20, EditingHostLifecycleEvent::kActivated},
                {20, EditingHostLifecycleEvent::kDeactivated},
                {20, EditingHostLifecycleEvent::kDetached}}));
}

TEST(EditingHostRegistryTest, RejectsDuplicateHostAndController) {
  EditingHostRegistry registry;
  auto controller =
      std::make_shared<EditingHostController>(EditContextOptions{});
  ASSERT_TRUE(registry.Attach(10, controller));

  EXPECT_FALSE(registry.Attach(10, std::make_shared<EditingHostController>(
                                       EditContextOptions{})));
  EXPECT_FALSE(registry.Attach(20, controller));
}

}  // namespace
}  // namespace lynx::editing
