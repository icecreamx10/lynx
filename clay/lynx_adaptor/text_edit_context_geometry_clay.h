// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CLAY_LYNX_ADAPTOR_TEXT_EDIT_CONTEXT_GEOMETRY_CLAY_H_
#define CLAY_LYNX_ADAPTOR_TEXT_EDIT_CONTEXT_GEOMETRY_CLAY_H_

#include <optional>
#include <unordered_map>
#include <vector>

#include "core/renderer/editing/editing_platform_contract.h"

namespace lynx::tasm {

struct ClayEditingMeasuredUnit {
  editing::EditingLayoutUnit unit;
  bool available{false};
};

struct ClayEditingOwnerLocalRange {
  size_t start{0};
  size_t end{0};
};

struct ClayEditingTextPlacement {
  int64_t segment_id{-1};
  int64_t owner_id{-1};
  editing::TextRange projection_range;
  ClayEditingOwnerLocalRange owner_range;
  bool available{false};
};

// Resolves each logical text segment into the glyph-bearing TextView's local
// range. InlineTextView paragraph ranges are authoritative. Other segments are
// located in document order using a separate cursor for each text owner.
std::vector<ClayEditingTextPlacement> ResolveClayTextPlacements(
    const editing::EditingProjectionSnapshot& projection,
    const std::unordered_map<int64_t, std::u16string>& owner_texts,
    const std::unordered_map<int64_t, ClayEditingOwnerLocalRange>&
        inline_ranges);

std::optional<size_t> MapClayProjectionOffsetToOwner(
    const std::vector<ClayEditingTextPlacement>& placements, int64_t owner_id,
    size_t projection_offset);

std::optional<size_t> MapClayOwnerOffsetToProjection(
    const std::vector<ClayEditingTextPlacement>& placements, int64_t owner_id,
    size_t owner_offset);

// Chooses the glyph-bearing text owner that should paint a collapsed caret.
// At a shared text boundary the preceding owner wins, matching the controller's
// trailing-edge-first selection-rect lookup.
std::optional<int64_t> FindClayCaretOwner(
    const std::vector<ClayEditingTextPlacement>& placements,
    size_t projection_offset);

// Block separators are logical projection units even though Clay has no glyph
// for them. Give each mounted separator a caret-shaped rectangle at the
// adjacent line edge so geometry remains contiguous across text owners.
void MeasureClayBlockBoundaries(
    const editing::EditingProjectionSnapshot& projection,
    std::vector<ClayEditingMeasuredUnit>* measured);

// Returns the maximal contiguous measured range containing a unit in the
// requested logical range. Missing units intentionally split coverage.
editing::TextRange ClayEditingCoverageForRange(
    const std::vector<ClayEditingMeasuredUnit>& measured,
    editing::TextRange requested);

// Chooses the maximal contiguous measured range whose units contain, or are
// nearest to, a viewport point. This lets pointer drag move between mounted
// owners even when another logical range was previously cached.
std::optional<editing::TextRange> ClayEditingCoverageForPoint(
    const std::vector<ClayEditingMeasuredUnit>& measured,
    editing::EditingLayoutPoint point);

}  // namespace lynx::tasm

#endif  // CLAY_LYNX_ADAPTOR_TEXT_EDIT_CONTEXT_GEOMETRY_CLAY_H_
