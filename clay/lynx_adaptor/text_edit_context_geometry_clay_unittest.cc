// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "clay/lynx_adaptor/text_edit_context_geometry_clay.h"

#include "core/renderer/editing/editing_host_controller.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx::tasm {
namespace {

ClayEditingMeasuredUnit Unit(size_t offset, float x, float y = 0) {
  ClayEditingMeasuredUnit result;
  result.available = true;
  result.unit.projection_offset = offset;
  result.unit.bounds = {x, y, 10, 20};
  return result;
}

TEST(TextEditContextGeometryClayTest, BlockBoundaryBridgesMountedTextOwners) {
  editing::EditingProjectionSnapshot projection;
  projection.length = 3;
  projection.segments = {
      {10, 100, editing::EditingSegmentKind::kText, u"a", u"a", 0, 1},
      {20, -1, editing::EditingSegmentKind::kBlockBoundary, u"\n", u"\n", 1, 2,
       true, editing::EditingBlockBoundaryEdge::kTrailing},
      {30, 200, editing::EditingSegmentKind::kText, u"b", u"b", 2, 3},
  };
  std::vector<ClayEditingMeasuredUnit> measured(3);
  measured[0] = Unit(0, 0);
  measured[2] = Unit(2, 0, 30);

  MeasureClayBlockBoundaries(projection, &measured);

  ASSERT_TRUE(measured[1].available);
  EXPECT_EQ(measured[1].unit.projection_offset, 1u);
  EXPECT_EQ(measured[1].unit.flags, editing::EditingLayoutUnitFlag::kBlock);
  EXPECT_EQ(measured[1].unit.boundary_edge,
            editing::EditingBlockBoundaryEdge::kTrailing);
  EXPECT_FLOAT_EQ(measured[1].unit.bounds.x, 10);
  EXPECT_FLOAT_EQ(measured[1].unit.bounds.width, 0);
  EXPECT_EQ(ClayEditingCoverageForRange(measured, editing::TextRange(0, 3)),
            editing::TextRange(0, 3));
}

TEST(TextEditContextGeometryClayTest, LeadingBoundaryUsesNextLineCaret) {
  editing::EditingProjectionSnapshot projection;
  projection.length = 2;
  projection.segments = {
      {20, -1, editing::EditingSegmentKind::kBlockBoundary, u"\n", u"\n", 0, 1,
       true, editing::EditingBlockBoundaryEdge::kLeading},
      {30, 200, editing::EditingSegmentKind::kText, u"b", u"b", 1, 2},
  };
  std::vector<ClayEditingMeasuredUnit> measured(2);
  measured[1] = Unit(1, 25, 40);

  MeasureClayBlockBoundaries(projection, &measured);

  ASSERT_TRUE(measured[0].available);
  EXPECT_FLOAT_EQ(measured[0].unit.bounds.x, 25);
  EXPECT_FLOAT_EQ(measured[0].unit.bounds.y, 40);
  EXPECT_FLOAT_EQ(measured[0].unit.bounds.width, 0);
}

TEST(TextEditContextGeometryClayTest, MissingOwnerSplitsLogicalCoverage) {
  std::vector<ClayEditingMeasuredUnit> measured(6);
  measured[0] = Unit(0, 0);
  measured[1] = Unit(1, 10);
  measured[4] = Unit(4, 100);
  measured[5] = Unit(5, 110);

  EXPECT_EQ(ClayEditingCoverageForRange(measured, editing::TextRange(0, 6)),
            editing::TextRange(0, 2));
  EXPECT_EQ(ClayEditingCoverageForRange(measured, editing::TextRange(4, 6)),
            editing::TextRange(4, 6));
}

TEST(TextEditContextGeometryClayTest, PointerSelectsNearestMountedOwnerRun) {
  std::vector<ClayEditingMeasuredUnit> measured(6);
  measured[0] = Unit(0, 0);
  measured[1] = Unit(1, 10);
  measured[4] = Unit(4, 100);
  measured[5] = Unit(5, 110);

  const auto coverage = ClayEditingCoverageForPoint(measured, {105, 10});

  ASSERT_TRUE(coverage.has_value());
  EXPECT_EQ(*coverage, editing::TextRange(4, 6));
}

TEST(TextEditContextGeometryClayTest, PointerReportsNoUnmountedGeometry) {
  EXPECT_FALSE(ClayEditingCoverageForPoint({}, {10, 10}).has_value());
}

TEST(TextEditContextGeometryClayTest,
     SameOwnerSegmentsUseOrderedTextCursorAndRoundTripOffsets) {
  editing::EditingProjectionSnapshot projection;
  projection.length = 6;
  projection.segments = {
      {10, 42, editing::EditingSegmentKind::kText, u"one", u"one", 0, 3},
      {20, 42, editing::EditingSegmentKind::kText, u"one", u"one", 3, 6},
  };

  const auto placements =
      ResolveClayTextPlacements(projection, {{42, u"one gap one"}}, {});

  ASSERT_EQ(placements.size(), 2u);
  ASSERT_TRUE(placements[0].available);
  ASSERT_TRUE(placements[1].available);
  EXPECT_EQ(placements[0].owner_range.start, 0u);
  EXPECT_EQ(placements[0].owner_range.end, 3u);
  EXPECT_EQ(placements[1].owner_range.start, 8u);
  EXPECT_EQ(placements[1].owner_range.end, 11u);
  EXPECT_EQ(MapClayProjectionOffsetToOwner(placements, 42, 4), 9u);
  EXPECT_EQ(MapClayOwnerOffsetToProjection(placements, 42, 9), 4u);
  EXPECT_EQ(MapClayOwnerOffsetToProjection(placements, 42, 5), 3u);
}

TEST(TextEditContextGeometryClayTest,
     InlineTextRangeOverridesEarlierMatchingOwnerText) {
  editing::EditingProjectionSnapshot projection;
  projection.length = 4;
  projection.segments = {
      {77, 42, editing::EditingSegmentKind::kText, u"same", u"same", 0, 4},
  };

  const auto placements = ResolveClayTextPlacements(
      projection, {{42, u"same gap same"}}, {{77, {9, 13}}});

  ASSERT_EQ(placements.size(), 1u);
  ASSERT_TRUE(placements[0].available);
  EXPECT_EQ(placements[0].owner_range.start, 9u);
  EXPECT_EQ(placements[0].owner_range.end, 13u);
  EXPECT_EQ(MapClayProjectionOffsetToOwner(placements, 42, 2), 11u);
  EXPECT_EQ(MapClayOwnerOffsetToProjection(placements, 42, 11), 2u);
}

TEST(TextEditContextGeometryClayTest,
     CaretOwnerUsesTrailingTextThenNextMountedText) {
  editing::EditingProjectionSnapshot projection;
  projection.length = 7;
  projection.segments = {
      {10, 100, editing::EditingSegmentKind::kText, u"abc", u"abc", 0, 3},
      {20, -1, editing::EditingSegmentKind::kAtomicObject, u"\uFFFC", u"atom",
       3, 4},
      {30, 200, editing::EditingSegmentKind::kText, u"xyz", u"xyz", 4, 7},
  };
  const auto placements =
      ResolveClayTextPlacements(projection, {{100, u"abc"}, {200, u"xyz"}}, {});

  EXPECT_EQ(FindClayCaretOwner(placements, 0), 100);
  EXPECT_EQ(FindClayCaretOwner(placements, 3), 100);
  EXPECT_EQ(FindClayCaretOwner(placements, 4), 200);
  EXPECT_EQ(FindClayCaretOwner(placements, 7), 200);
}

TEST(TextEditContextGeometryClayTest,
     ControllerExtendsSelectionAcrossTextOwnersAndBoundary) {
  editing::EditContextOptions options;
  options.text = u"a\nb";
  editing::EditingHostController controller(std::move(options));
  editing::EditingProjection projection;
  projection.AppendText(100, u"a", 10);
  projection.AppendBlockBoundary(20,
                                 editing::EditingBlockBoundaryEdge::kTrailing);
  projection.AppendText(200, u"b", 30);
  controller.SetProjection(std::move(projection), 1);
  controller.Attach();
  ASSERT_TRUE(controller.Activate());

  editing::EditingProjectionSnapshot snapshot = controller.ProjectionSnapshot();
  std::vector<ClayEditingMeasuredUnit> measured(3);
  measured[0] = Unit(0, 0);
  measured[0].unit.owner_id = 100;
  measured[2] = Unit(2, 0, 30);
  measured[2].unit.owner_id = 200;
  MeasureClayBlockBoundaries(snapshot, &measured);
  editing::EditingGeometrySnapshot geometry;
  geometry.projection_revision = 1;
  geometry.projection_length = 3;
  geometry.coverage = ClayEditingCoverageForRange(
      measured, editing::TextRange(0, measured.size()));
  geometry.control_bounds = {0, 0, 100, 100};
  for (const ClayEditingMeasuredUnit& unit : measured) {
    geometry.units.push_back(unit.unit);
  }
  ASSERT_EQ(controller.UpdateGeometry(std::move(geometry)),
            editing::EditingOperationStatus::kAccepted);

  const editing::EditingPlatformResult result =
      controller.SetSelectionFromPoint({9, 40}, 0, 0);

  ASSERT_TRUE(result.accepted());
  EXPECT_EQ(result.snapshot.selection, editing::TextRange(0, 3));
}

}  // namespace
}  // namespace lynx::tasm

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
