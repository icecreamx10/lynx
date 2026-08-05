// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/editing_platform_contract.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx::editing {
namespace {

EditingStateSnapshot Snapshot(std::u16string text, uint64_t revision) {
  EditingStateSnapshot snapshot;
  snapshot.text = std::move(text);
  snapshot.selection = TextRange(0);
  snapshot.revision = revision;
  return snapshot;
}

TEST(EditingPlatformContractTest, RejectsStaleRevisionBeforeRanges) {
  NativeTextTransaction transaction;
  transaction.expected_revision = 6;
  transaction.updates_text = true;
  transaction.replacement_range = TextRange(100, 200);

  EXPECT_EQ(ValidateTransaction(Snapshot(u"abc", 7), transaction),
            EditingOperationStatus::kStaleRevision);
}

TEST(EditingPlatformContractTest, ValidatesRangesAgainstResultingUtf16Text) {
  NativeTextTransaction transaction;
  transaction.expected_revision = 7;
  transaction.updates_text = true;
  transaction.replacement_range = TextRange(1, 2);
  transaction.replacement_text = u"\U0001F600";
  transaction.selection = TextRange(3, 1);
  transaction.composition = TextRange(1, 3);

  EXPECT_EQ(ValidateTransaction(Snapshot(u"abc", 7), transaction),
            EditingOperationStatus::kAccepted);
  transaction.selection = TextRange(5);
  EXPECT_EQ(ValidateTransaction(Snapshot(u"abc", 7), transaction),
            EditingOperationStatus::kInvalidRange);
}

TEST(EditingPlatformContractTest, PreservesBackwardSelection) {
  NativeTextTransaction transaction;
  transaction.expected_revision = 1;
  transaction.selection = TextRange(3, 1);

  EXPECT_TRUE(transaction.selection.reversed());
  EXPECT_EQ(ValidateTransaction(Snapshot(u"abcd", 1), transaction),
            EditingOperationStatus::kAccepted);
}

TEST(EditingPlatformContractTest, PartialGeometryIsExplicitAndValid) {
  EditingGeometrySnapshot geometry;
  geometry.state_revision = 9;
  geometry.projection_revision = 4;
  geometry.projection_length = 1000;
  geometry.coverage = TextRange(400, 420);
  geometry.units.push_back({400,
                            10,
                            20,
                            0,
                            1,
                            {0, 0, 10, 10},
                            EditingLayoutUnitFlag::kHasDirection,
                            EditingBlockBoundaryEdge::kNone});
  geometry.units.push_back({419,
                            11,
                            21,
                            0,
                            1,
                            {10, 0, 10, 10},
                            EditingLayoutUnitFlag::kHasDirection,
                            EditingBlockBoundaryEdge::kNone});

  EXPECT_TRUE(geometry.IsStructurallyValid());
  EXPECT_TRUE(geometry.Covers(TextRange(405, 415)));
  EXPECT_FALSE(geometry.Covers(TextRange(399, 415)));
  EXPECT_FALSE(geometry.Covers(TextRange(415, 421)));
}

TEST(EditingPlatformContractTest, RejectsOverlappingOrUnorderedGeometry) {
  EditingGeometrySnapshot geometry;
  geometry.projection_length = 10;
  geometry.coverage = TextRange(2, 5);
  geometry.units.push_back({3, 1, 1, 0, 1, {0, 0, 1, 1}});
  geometry.units.push_back({3, 1, 1, 1, 2, {1, 0, 1, 1}});

  EXPECT_FALSE(geometry.IsStructurallyValid());
}

TEST(EditingPlatformContractTest, RejectsReversedReplacementRange) {
  EditingStateSnapshot snapshot;
  snapshot.text = u"abc";
  NativeTextTransaction transaction{
      "insertText", true, TextRange(2, 1), u"x", TextRange(2), std::nullopt, 0};

  EXPECT_EQ(ValidateTransaction(snapshot, transaction),
            EditingOperationStatus::kInvalidRange);
}

TEST(EditingPlatformContractTest, ProjectionSnapshotIsMeasurementOnlyData) {
  EditingProjectionSnapshot projection;
  projection.revision = 8;
  projection.length = 3;
  projection.segments.push_back(
      {10, 20, EditingSegmentKind::kText, u"abc", u"abc", 0, 3});

  EXPECT_EQ(projection.revision, 8u);
  EXPECT_EQ(projection.length, 3u);
  ASSERT_EQ(projection.segments.size(), 1u);
  EXPECT_EQ(projection.segments[0].owner_id, 20);
}

}  // namespace
}  // namespace lynx::editing
