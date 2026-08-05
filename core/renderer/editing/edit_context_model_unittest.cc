// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/edit_context_model.h"

#include <vector>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx::editing {
namespace {

TEST(EditContextModelTest, NativeCompositionDispatchesOrderedEvents) {
  EditContextModel model({u"abc", 1, 1});
  std::vector<EditContextEventType> events;
  std::vector<uint64_t> revisions;
  model.SetEventCallback([&events](const EditContextEvent& event) {
    events.push_back(event.type);
  });
  model.SetStateChangeCallback(
      [&revisions](const EditingStateSnapshot& snapshot) {
        revisions.push_back(snapshot.revision);
      });

  EXPECT_TRUE(
      model.ApplyNativeTextUpdate(1, 1, u"x", TextRange(2), TextRange(1, 2)));
  EXPECT_TRUE(
      model.ApplyNativeTextUpdate(1, 2, u"y", TextRange(2), std::nullopt));

  EXPECT_EQ(events, (std::vector<EditContextEventType>{
                        EditContextEventType::kCompositionStart,
                        EditContextEventType::kTextUpdate,
                        EditContextEventType::kTextUpdate,
                        EditContextEventType::kCompositionEnd}));
  EXPECT_EQ(revisions, (std::vector<uint64_t>{1, 2}));
  EXPECT_EQ(model.text(), u"aybc");
  EXPECT_FALSE(model.composition().has_value());
}

TEST(EditContextModelTest, InvalidNativeUpdateHasNoSideEffects) {
  EditContextModel model({u"abc", 1, 1});
  size_t callback_count = 0;
  model.SetStateChangeCallback(
      [&callback_count](const EditingStateSnapshot&) { ++callback_count; });

  EXPECT_FALSE(
      model.ApplyNativeTextUpdate(3, 1, u"x", TextRange(2), std::nullopt));
  EXPECT_EQ(model.text(), u"abc");
  EXPECT_EQ(model.revision(), 0u);
  EXPECT_EQ(callback_count, 0u);
}

}  // namespace
}  // namespace lynx::editing
