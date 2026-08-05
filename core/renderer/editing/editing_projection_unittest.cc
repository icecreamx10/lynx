// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/editing_projection.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx::editing {
namespace {

TEST(EditingProjectionTest, SelectionCanSpanMultipleTextOwners) {
  EditingProjection projection;
  projection.AppendText(101, u"first");
  projection.AppendBlockBoundary(201, EditingBlockBoundaryEdge::kTrailing);
  projection.AppendText(102, u"second");

  EXPECT_EQ(projection.text(), u"first\nsecond");
  EXPECT_EQ(projection.SerializePlainText(TextRange(2, 9)), u"rst\nsec");

  EditingNodePosition first =
      projection.MapOffset(2, EditingOffsetAffinity::kDownstream);
  EditingNodePosition second =
      projection.MapOffset(9, EditingOffsetAffinity::kDownstream);
  EXPECT_TRUE(first.valid);
  EXPECT_EQ(first.owner_id, 101);
  EXPECT_EQ(first.offset, 2u);
  EXPECT_TRUE(second.valid);
  EXPECT_EQ(second.owner_id, 102);
  EXPECT_EQ(second.offset, 3u);
}

TEST(EditingProjectionTest, AtomicObjectUsesPlainTextForClipboard) {
  EditingProjection projection;
  projection.AppendText(101, u"before ");
  projection.AppendAtomicObject(301, u"@lynx", false);
  projection.AppendText(102, u" after");

  EXPECT_EQ(projection.text(), u"before \uFFFC after");
  EXPECT_EQ(
      projection.SerializePlainText(TextRange(0, projection.text().size())),
      u"before @lynx after");
}

TEST(EditingProjectionTest, BoundaryAffinitySelectsAdjacentOwner) {
  EditingProjection projection;
  projection.AppendText(101, u"a");
  projection.AppendText(102, u"b");

  EditingNodePosition upstream =
      projection.MapOffset(1, EditingOffsetAffinity::kUpstream);
  EditingNodePosition downstream =
      projection.MapOffset(1, EditingOffsetAffinity::kDownstream);
  EXPECT_EQ(upstream.owner_id, 101);
  EXPECT_EQ(upstream.offset, 1u);
  EXPECT_EQ(downstream.owner_id, 102);
  EXPECT_EQ(downstream.offset, 0u);
}

}  // namespace
}  // namespace lynx::editing
