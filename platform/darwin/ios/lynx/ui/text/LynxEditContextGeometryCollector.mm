// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import "LynxEditContextGeometryCollector.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace {

using lynx::editing::EditingBlockBoundaryEdge;
using lynx::editing::EditingLayoutUnit;
using lynx::editing::EditingLayoutUnitFlag;
using lynx::editing::EditingProjectionSnapshot;
using lynx::editing::EditingSegment;
using lynx::editing::EditingSegmentKind;

EditingLayoutUnitFlag AddFlag(EditingLayoutUnitFlag flags,
                              EditingLayoutUnitFlag added) {
  return static_cast<EditingLayoutUnitFlag>(static_cast<uint8_t>(flags) |
                                            static_cast<uint8_t>(added));
}

lynx::editing::EditContextRect ToEditContextRect(CGRect rect) {
  return {static_cast<float>(rect.origin.x), static_cast<float>(rect.origin.y),
          static_cast<float>(rect.size.width), static_cast<float>(rect.size.height)};
}

}  // namespace

BOOL LynxCollectEditContextGeometry(
    id<LynxEditContextGeometryResolver> resolver, CGRect controlBounds,
    NSRange requestedRange, uint64_t stateRevision,
    const EditingProjectionSnapshot &projection,
    lynx::editing::EditingGeometrySnapshot *snapshot) {
  if (!resolver || !snapshot || requestedRange.location == NSNotFound ||
      requestedRange.location > projection.length ||
      requestedRange.length > projection.length - requestedRange.location) {
    return NO;
  }

  snapshot->state_revision = stateRevision;
  snapshot->projection_revision = projection.revision;
  snapshot->projection_length = projection.length;
  snapshot->control_bounds = ToEditContextRect(controlBounds);
  snapshot->coverage = lynx::editing::TextRange();
  snapshot->units.clear();

  if (projection.length == 0) {
    return YES;
  }

  std::vector<std::optional<EditingLayoutUnit>> measured(projection.length);
  for (const EditingSegment &segment : projection.segments) {
    if (segment.start > segment.end || segment.end > projection.length) {
      return NO;
    }
    for (size_t offset = segment.start; offset < segment.end; ++offset) {
      CGRect bounds = CGRectZero;
      BOOL rightToLeft = NO;
      BOOL available = NO;
      const size_t localOffset = offset - segment.start;
      if (segment.kind == EditingSegmentKind::kText) {
        available = [resolver editContextMeasureTextSegment:segment.segment_id
                                                    ownerID:segment.owner_id
                                                localOffset:localOffset
                                                     bounds:&bounds
                                                rightToLeft:&rightToLeft];
      } else {
        available = [resolver editContextMeasureNode:segment.segment_id bounds:&bounds];
        if (available && segment.kind == EditingSegmentKind::kBlockBoundary) {
          if (segment.boundary_edge == EditingBlockBoundaryEdge::kTrailing) {
            bounds.origin.x = CGRectGetMaxX(bounds);
          }
          bounds.size.width = 0;
        }
      }
      if (!available) {
        continue;
      }

      EditingLayoutUnitFlag flags = EditingLayoutUnitFlag::kNone;
      if (rightToLeft) {
        flags = AddFlag(flags, EditingLayoutUnitFlag::kHasDirection);
        flags = AddFlag(flags, EditingLayoutUnitFlag::kRightToLeft);
      }
      if (segment.kind == EditingSegmentKind::kAtomicObject) {
        flags = AddFlag(flags, EditingLayoutUnitFlag::kAtomic);
      }
      if (segment.block || segment.kind == EditingSegmentKind::kBlockBoundary) {
        flags = AddFlag(flags, EditingLayoutUnitFlag::kBlock);
      }

      EditingLayoutUnit unit;
      unit.projection_offset = offset;
      unit.segment_id = segment.segment_id;
      unit.owner_id = segment.owner_id;
      unit.local_start = localOffset;
      unit.local_end = localOffset + 1;
      unit.bounds = ToEditContextRect(bounds);
      unit.flags = flags;
      unit.boundary_edge = segment.boundary_edge;
      measured[offset] = unit;
    }
  }

  size_t searchStart = requestedRange.location;
  size_t searchEnd = NSMaxRange(requestedRange);
  if (requestedRange.length == 0) {
    searchStart = std::min<size_t>(requestedRange.location, projection.length - 1);
    searchEnd = searchStart + 1;
  }
  size_t seed = projection.length;
  for (size_t offset = searchStart; offset < searchEnd; ++offset) {
    if (measured[offset]) {
      seed = offset;
      break;
    }
  }
  if (seed == projection.length) {
    return NO;
  }

  size_t coverageStart = seed;
  size_t coverageEnd = seed + 1;
  while (coverageStart > 0 && measured[coverageStart - 1]) {
    --coverageStart;
  }
  while (coverageEnd < projection.length && measured[coverageEnd]) {
    ++coverageEnd;
  }
  snapshot->coverage = lynx::editing::TextRange(coverageStart, coverageEnd);
  snapshot->units.reserve(coverageEnd - coverageStart);
  for (size_t offset = coverageStart; offset < coverageEnd; ++offset) {
    snapshot->units.push_back(*measured[offset]);
  }
  return snapshot->IsStructurallyValid();
}
