// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/editing_host_controller.h"

#include <string>
#include <vector>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx::editing {
namespace {

class TestPlatformDelegate final : public EditingPlatformDelegate {
 public:
  void OnStateChanged(const EditingStateUpdate& update) override {
    updates.push_back(update);
  }
  void OnActivationChanged(bool active) override {
    activations.push_back(active);
  }
  void OnGeometryRequested(const TextRange& range, uint64_t state_revision,
                           uint64_t projection_revision) override {
    requested_ranges.push_back(range);
    requested_state_revision = state_revision;
    requested_projection_revision = projection_revision;
  }

  std::vector<EditingStateUpdate> updates;
  std::vector<bool> activations;
  std::vector<TextRange> requested_ranges;
  uint64_t requested_state_revision{0};
  uint64_t requested_projection_revision{0};
};

EditingHostController Controller(std::u16string text) {
  EditContextOptions options;
  options.text = std::move(text);
  return EditingHostController(std::move(options));
}

TEST(EditingHostControllerTest, RequiresAttachBeforeActivation) {
  EditingHostController controller = Controller(u"abc");
  TestPlatformDelegate delegate;
  controller.SetDelegate(&delegate);

  EXPECT_FALSE(controller.Activate());
  controller.Attach();
  EXPECT_TRUE(controller.Activate());
  EXPECT_TRUE(controller.IsActive());
  ASSERT_EQ(delegate.activations.size(), 1u);
  EXPECT_TRUE(delegate.activations[0]);

  controller.Deactivate();
  controller.Deactivate();
  ASSERT_EQ(delegate.activations.size(), 2u);
  EXPECT_FALSE(delegate.activations[1]);
}

TEST(EditingHostControllerTest, NativeTransactionIsAuthoritativeAndVersioned) {
  EditingHostController controller = Controller(u"abc");
  controller.Attach();
  controller.Activate();
  NativeTextTransaction transaction{
      "insertText", true, TextRange(1, 2), u"XY", TextRange(3),
      std::nullopt, 0};

  EditingPlatformResult accepted = controller.ApplyTransaction(transaction);
  EXPECT_TRUE(accepted.accepted());
  EXPECT_EQ(accepted.snapshot.text, u"aXYc");
  EXPECT_EQ(accepted.snapshot.selection, TextRange(3));
  EXPECT_EQ(accepted.snapshot.revision, 1u);

  EditingPlatformResult stale = controller.ApplyTransaction(transaction);
  EXPECT_EQ(stale.status, EditingOperationStatus::kStaleRevision);
  EXPECT_TRUE(stale.restart_input);
  EXPECT_EQ(stale.snapshot.text, u"aXYc");
}

TEST(EditingHostControllerTest, InactiveStatusPrecedesRevisionValidation) {
  EditingHostController controller = Controller(u"abc");

  EXPECT_EQ(controller.PerformInput("insertText", u"x", 99).status,
            EditingOperationStatus::kInactive);
  EXPECT_EQ(controller.SetSelectionFromPoint({0, 0}, std::nullopt, 99).status,
            EditingOperationStatus::kInactive);
  EXPECT_EQ(controller.Snapshot().revision, 0u);
}

TEST(EditingHostControllerTest, CancelledBeforeInputDoesNotMutateState) {
  EditingHostController controller = Controller(u"abc");
  controller.Attach();
  controller.Activate();
  controller.SetBeforeInputCallback(
      [](const EditingInputEvent&) { return false; });

  EditingPlatformResult result = controller.PerformInput("insertText", u"x", 0);
  EXPECT_TRUE(result.accepted());
  EXPECT_EQ(result.snapshot.text, u"abc");
  EXPECT_EQ(result.snapshot.revision, 0u);
}

TEST(EditingHostControllerTest, DeleteBackwardDoesNotSplitSurrogatePair) {
  EditingHostController controller = Controller(u"A\U0001F600B");
  controller.Attach();
  controller.Activate();
  ASSERT_TRUE(controller.edit_context().UpdateSelection(3, 3));

  EditingPlatformResult result =
      controller.PerformInput("deleteContentBackward", u"", 1);
  EXPECT_TRUE(result.accepted());
  EXPECT_EQ(result.snapshot.text, u"AB");
  EXPECT_EQ(result.snapshot.selection, TextRange(1));
}

TEST(EditingHostControllerTest, GeometryRequiresBothRevisions) {
  EditingHostController controller = Controller(u"abc");
  EditingProjection projection;
  projection.AppendText(20, u"abc", 10);
  controller.SetProjection(std::move(projection), 4);
  controller.Attach();
  controller.Activate();

  EditingGeometrySnapshot geometry;
  geometry.state_revision = 0;
  geometry.projection_revision = 3;
  geometry.projection_length = 3;
  geometry.coverage = TextRange(0, 3);
  EXPECT_EQ(controller.UpdateGeometry(geometry),
            EditingOperationStatus::kStaleRevision);
  geometry.projection_revision = 4;
  geometry.units.push_back({0, 10, 20, 0, 1, {0, 0, 10, 10}});
  EXPECT_EQ(controller.UpdateGeometry(std::move(geometry)),
            EditingOperationStatus::kAccepted);
}

TEST(EditingHostControllerTest, MissingGeometryRequestsHostMeasurement) {
  EditingHostController controller = Controller(u"abc");
  EditingProjection projection;
  projection.AppendText(20, u"abc", 10);
  controller.SetProjection(std::move(projection), 4);
  TestPlatformDelegate delegate;
  controller.SetDelegate(&delegate);
  controller.Attach();
  controller.Activate();

  EditingPlatformResult result =
      controller.SetSelectionFromPoint({10, 10}, std::nullopt, 0);
  EXPECT_EQ(result.status, EditingOperationStatus::kGeometryUnavailable);
  ASSERT_EQ(delegate.requested_ranges.size(), 1u);
  EXPECT_EQ(delegate.requested_ranges[0], TextRange(0, 3));
  EXPECT_EQ(delegate.requested_projection_revision, 4u);
}

TEST(EditingHostControllerTest, SelectionRectsRequestMissingCoverage) {
  EditingHostController controller = Controller(u"abcdef");
  EditingProjection projection;
  projection.AppendText(20, u"abcdef", 10);
  controller.SetProjection(std::move(projection), 4);
  TestPlatformDelegate delegate;
  controller.SetDelegate(&delegate);
  controller.Attach();
  controller.Activate();

  EditingGeometrySnapshot geometry;
  geometry.projection_revision = 4;
  geometry.projection_length = 6;
  geometry.coverage = TextRange(0, 3);
  geometry.units = {
      {0, 10, 20, 0, 1, {0, 0, 10, 10}},
      {1, 10, 20, 1, 2, {10, 0, 10, 10}},
      {2, 10, 20, 2, 3, {20, 0, 10, 10}},
  };
  ASSERT_EQ(controller.UpdateGeometry(std::move(geometry)),
            EditingOperationStatus::kAccepted);

  EditingSelectionRectsResult result =
      controller.QuerySelectionRects(TextRange(1, 5), 0);
  EXPECT_EQ(result.status, EditingOperationStatus::kGeometryUnavailable);
  EXPECT_TRUE(result.rects.empty());
  ASSERT_FALSE(delegate.requested_ranges.empty());
  EXPECT_EQ(delegate.requested_ranges.back(), TextRange(1, 5));
}

TEST(EditingHostControllerTest, CollapsedSelectionReturnsCaretRect) {
  EditingHostController controller = Controller(u"a");
  EditingProjection projection;
  projection.AppendText(20, u"a", 10);
  controller.SetProjection(std::move(projection), 4);
  controller.Attach();
  controller.Activate();
  EditingGeometrySnapshot geometry;
  geometry.projection_revision = 4;
  geometry.projection_length = 1;
  geometry.coverage = TextRange(0, 1);
  geometry.units = {{0, 10, 20, 0, 1, {5, 6, 8, 10}}};
  ASSERT_EQ(controller.UpdateGeometry(std::move(geometry)),
            EditingOperationStatus::kAccepted);

  auto result = controller.QuerySelectionRects(TextRange(1), 0);
  ASSERT_EQ(result.status, EditingOperationStatus::kAccepted);
  ASSERT_EQ(result.rects.size(), 1u);
  EXPECT_EQ(result.rects[0].x, 13);
  EXPECT_EQ(result.rects[0].width, 0);
}

TEST(EditingHostControllerTest, EmptyDocumentUsesControlBoundsForCaret) {
  EditingHostController controller = Controller(u"");
  controller.SetProjection(EditingProjection(), 4);
  controller.Attach();
  controller.Activate();
  EditingGeometrySnapshot geometry;
  geometry.projection_revision = 4;
  geometry.coverage = TextRange(0);
  geometry.control_bounds = {5, 6, 80, 18};
  ASSERT_EQ(controller.UpdateGeometry(std::move(geometry)),
            EditingOperationStatus::kAccepted);

  auto result = controller.QuerySelectionRects(TextRange(0), 0);
  ASSERT_EQ(result.status, EditingOperationStatus::kAccepted);
  ASSERT_EQ(result.rects.size(), 1u);
  EXPECT_EQ(result.rects[0].x, 5);
  EXPECT_EQ(result.rects[0].height, 18);
  EXPECT_EQ(result.rects[0].width, 0);
}

}  // namespace
}  // namespace lynx::editing
