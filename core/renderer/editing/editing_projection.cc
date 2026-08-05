// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/editing_projection.h"

#include <algorithm>

namespace lynx::editing {

namespace {
constexpr char16_t kObjectReplacementCharacter = u'\uFFFC';
constexpr char16_t kBlockSeparator = u'\n';
}  // namespace

void EditingProjection::Clear() {
  text_.clear();
  segments_.clear();
}

void EditingProjection::AppendText(int64_t owner_id, const std::u16string& text,
                                   std::optional<int64_t> segment_id) {
  AppendSegment(segment_id.value_or(owner_id), owner_id,
                EditingSegmentKind::kText, text, text, false,
                EditingBlockBoundaryEdge::kNone);
}

void EditingProjection::AppendAtomicObject(int64_t segment_id,
                                           const std::u16string& plain_text,
                                           bool block) {
  AppendSegment(segment_id, -1, EditingSegmentKind::kAtomicObject,
                std::u16string(1, kObjectReplacementCharacter), plain_text,
                block, EditingBlockBoundaryEdge::kNone);
}

void EditingProjection::AppendBlockBoundary(int64_t segment_id,
                                            EditingBlockBoundaryEdge edge) {
  AppendSegment(segment_id, -1, EditingSegmentKind::kBlockBoundary,
                std::u16string(1, kBlockSeparator),
                std::u16string(1, kBlockSeparator), true, edge);
}

void EditingProjection::AppendSegment(int64_t segment_id, int64_t owner_id,
                                      EditingSegmentKind kind,
                                      const std::u16string& text,
                                      const std::u16string& plain_text,
                                      bool block,
                                      EditingBlockBoundaryEdge edge) {
  const size_t start = text_.size();
  text_ += text;
  segments_.push_back({segment_id, owner_id, kind, text, plain_text, start,
                       text_.size(), block, edge});
}

EditingNodePosition EditingProjection::MapOffset(
    size_t offset, EditingOffsetAffinity affinity) const {
  if (offset > text_.size()) {
    return {};
  }
  for (size_t index = 0; index < segments_.size(); ++index) {
    const EditingSegment& segment = segments_[index];
    const bool at_end = offset == segment.end;
    if (segment.Contains(offset) ||
        (at_end && affinity == EditingOffsetAffinity::kUpstream)) {
      if (segment.kind != EditingSegmentKind::kText || segment.owner_id < 0) {
        return {};
      }
      return {segment.owner_id, std::min(offset, segment.end) - segment.start,
              true};
    }
  }
  return {};
}

std::u16string EditingProjection::SerializePlainText(
    const TextRange& range) const {
  const size_t start = std::min(range.start(), text_.size());
  const size_t end = std::min(range.end(), text_.size());
  std::u16string result;
  for (const EditingSegment& segment : segments_) {
    const size_t overlap_start = std::max(start, segment.start);
    const size_t overlap_end = std::min(end, segment.end);
    if (overlap_start >= overlap_end) {
      continue;
    }
    if (segment.kind == EditingSegmentKind::kText) {
      result.append(segment.text, overlap_start - segment.start,
                    overlap_end - overlap_start);
    } else {
      result += segment.plain_text;
    }
  }
  return result;
}

}  // namespace lynx::editing
