// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "clay/lynx_adaptor/text_edit_context_geometry_clay.h"

#include <algorithm>
#include <limits>

namespace lynx::tasm {
namespace {

editing::TextRange CoverageContaining(
    const std::vector<ClayEditingMeasuredUnit>& measured, size_t seed) {
  size_t start = seed;
  while (start > 0 && measured[start - 1].available) {
    --start;
  }
  size_t end = seed + 1;
  while (end < measured.size() && measured[end].available) {
    ++end;
  }
  return editing::TextRange(start, end);
}

bool Contains(const editing::EditContextRect& rect,
              editing::EditingLayoutPoint point) {
  return point.x >= rect.x && point.x <= rect.x + rect.width &&
         point.y >= rect.y && point.y <= rect.y + rect.height;
}

float DistanceSquared(const editing::EditContextRect& rect,
                      editing::EditingLayoutPoint point) {
  const float center_x = rect.x + rect.width * 0.5f;
  const float center_y = rect.y + rect.height * 0.5f;
  const float dx = center_x - point.x;
  const float dy = center_y - point.y;
  return dx * dx + dy * dy;
}

}  // namespace

std::vector<ClayEditingTextPlacement> ResolveClayTextPlacements(
    const editing::EditingProjectionSnapshot& projection,
    const std::unordered_map<int64_t, std::u16string>& owner_texts,
    const std::unordered_map<int64_t, ClayEditingOwnerLocalRange>&
        inline_ranges) {
  std::vector<ClayEditingTextPlacement> placements;
  placements.reserve(projection.segments.size());
  std::unordered_map<int64_t, size_t> owner_cursors;
  for (const editing::EditingSegment& segment : projection.segments) {
    ClayEditingTextPlacement placement;
    placement.segment_id = segment.segment_id;
    placement.owner_id = segment.owner_id;
    placement.projection_range = editing::TextRange(segment.start, segment.end);
    if (segment.kind != editing::EditingSegmentKind::kText ||
        segment.start > segment.end || segment.end > projection.length) {
      placements.push_back(std::move(placement));
      continue;
    }
    const auto owner = owner_texts.find(segment.owner_id);
    if (owner == owner_texts.end()) {
      placements.push_back(std::move(placement));
      continue;
    }
    const size_t length = segment.end - segment.start;
    size_t owner_start = std::u16string::npos;
    const auto inline_range = inline_ranges.find(segment.segment_id);
    if (inline_range != inline_ranges.end() &&
        inline_range->second.start <= inline_range->second.end &&
        inline_range->second.end - inline_range->second.start >= length &&
        inline_range->second.start + length <= owner->second.size()) {
      owner_start = inline_range->second.start;
    } else {
      const size_t cursor = owner_cursors[segment.owner_id];
      if (segment.text.size() == length) {
        owner_start = owner->second.find(segment.text, cursor);
      }
    }
    if (owner_start != std::u16string::npos &&
        owner_start + length <= owner->second.size()) {
      placement.owner_range = {owner_start, owner_start + length};
      placement.available = true;
      owner_cursors[segment.owner_id] =
          std::max(owner_cursors[segment.owner_id], owner_start + length);
    }
    placements.push_back(std::move(placement));
  }
  return placements;
}

std::optional<size_t> MapClayProjectionOffsetToOwner(
    const std::vector<ClayEditingTextPlacement>& placements, int64_t owner_id,
    size_t projection_offset) {
  std::optional<size_t> owner_offset;
  for (const ClayEditingTextPlacement& placement : placements) {
    if (!placement.available || placement.owner_id != owner_id) {
      continue;
    }
    if (projection_offset <= placement.projection_range.start()) {
      return placement.owner_range.start;
    }
    if (projection_offset <= placement.projection_range.end()) {
      return placement.owner_range.start + projection_offset -
             placement.projection_range.start();
    }
    owner_offset = placement.owner_range.end;
  }
  return owner_offset;
}

std::optional<size_t> MapClayOwnerOffsetToProjection(
    const std::vector<ClayEditingTextPlacement>& placements, int64_t owner_id,
    size_t owner_offset) {
  std::optional<size_t> projection_offset;
  for (const ClayEditingTextPlacement& placement : placements) {
    if (!placement.available || placement.owner_id != owner_id) {
      continue;
    }
    if (owner_offset < placement.owner_range.start) {
      return projection_offset.value_or(placement.projection_range.start());
    }
    if (owner_offset <= placement.owner_range.end) {
      return placement.projection_range.start() +
             std::min(owner_offset - placement.owner_range.start,
                      placement.projection_range.length());
    }
    projection_offset = placement.projection_range.end();
  }
  return projection_offset;
}

std::optional<int64_t> FindClayCaretOwner(
    const std::vector<ClayEditingTextPlacement>& placements,
    size_t projection_offset) {
  std::optional<int64_t> leading_owner;
  for (const ClayEditingTextPlacement& placement : placements) {
    if (!placement.available) {
      continue;
    }
    if (projection_offset > placement.projection_range.start() &&
        projection_offset <= placement.projection_range.end()) {
      return placement.owner_id;
    }
    if (!leading_owner &&
        projection_offset == placement.projection_range.start()) {
      leading_owner = placement.owner_id;
    }
  }
  return leading_owner;
}

void MeasureClayBlockBoundaries(
    const editing::EditingProjectionSnapshot& projection,
    std::vector<ClayEditingMeasuredUnit>* measured) {
  if (!measured || measured->size() != projection.length) {
    return;
  }
  for (const editing::EditingSegment& segment : projection.segments) {
    if (segment.kind != editing::EditingSegmentKind::kBlockBoundary ||
        segment.end != segment.start + 1 || segment.end > measured->size()) {
      continue;
    }
    const ClayEditingMeasuredUnit* previous =
        segment.start > 0 && (*measured)[segment.start - 1].available
            ? &(*measured)[segment.start - 1]
            : nullptr;
    const ClayEditingMeasuredUnit* next =
        segment.end < measured->size() && (*measured)[segment.end].available
            ? &(*measured)[segment.end]
            : nullptr;
    const ClayEditingMeasuredUnit* reference = nullptr;
    bool use_trailing_edge = false;
    if (segment.boundary_edge == editing::EditingBlockBoundaryEdge::kLeading &&
        next) {
      reference = next;
    } else if (previous) {
      reference = previous;
      use_trailing_edge = true;
    } else if (next) {
      reference = next;
    }
    if (!reference) {
      continue;
    }

    ClayEditingMeasuredUnit& output = (*measured)[segment.start];
    output.available = true;
    output.unit.projection_offset = segment.start;
    output.unit.segment_id = segment.segment_id;
    output.unit.owner_id = segment.owner_id;
    output.unit.local_start = 0;
    output.unit.local_end = 1;
    output.unit.bounds = reference->unit.bounds;
    if (use_trailing_edge) {
      output.unit.bounds.x += output.unit.bounds.width;
    }
    output.unit.bounds.width = 0;
    output.unit.flags = editing::EditingLayoutUnitFlag::kBlock;
    output.unit.boundary_edge = segment.boundary_edge;
  }
}

editing::TextRange ClayEditingCoverageForRange(
    const std::vector<ClayEditingMeasuredUnit>& measured,
    editing::TextRange requested) {
  if (measured.empty()) {
    return editing::TextRange(0);
  }
  const size_t begin = std::min(requested.start(), measured.size());
  const size_t end = std::min(requested.end(), measured.size());
  for (size_t index = begin; index < end; ++index) {
    if (measured[index].available) {
      return CoverageContaining(measured, index);
    }
  }
  size_t seed = begin == measured.size() ? measured.size() - 1 : begin;
  if (measured[seed].available) {
    return CoverageContaining(measured, seed);
  }
  return editing::TextRange(seed);
}

std::optional<editing::TextRange> ClayEditingCoverageForPoint(
    const std::vector<ClayEditingMeasuredUnit>& measured,
    editing::EditingLayoutPoint point) {
  std::optional<size_t> best;
  float best_distance = std::numeric_limits<float>::max();
  for (size_t index = 0; index < measured.size(); ++index) {
    if (!measured[index].available) {
      continue;
    }
    const editing::EditContextRect& bounds = measured[index].unit.bounds;
    if (Contains(bounds, point)) {
      best = index;
      break;
    }
    const float distance = DistanceSquared(bounds, point);
    if (distance < best_distance) {
      best_distance = distance;
      best = index;
    }
  }
  return best ? std::optional<editing::TextRange>(
                    CoverageContaining(measured, *best))
              : std::nullopt;
}

}  // namespace lynx::tasm
