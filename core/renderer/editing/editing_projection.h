// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_EDITING_EDITING_PROJECTION_H_
#define CORE_RENDERER_EDITING_EDITING_PROJECTION_H_

#include <optional>
#include <string>
#include <vector>

#include "core/renderer/editing/editing_types.h"

namespace lynx::editing {

class EditingProjection {
 public:
  EditingProjection() = default;
  EditingProjection(EditingProjection&&) = default;
  EditingProjection& operator=(EditingProjection&&) = default;

  void Clear();
  void AppendText(int64_t owner_id, const std::u16string& text,
                  std::optional<int64_t> segment_id = std::nullopt);
  void AppendAtomicObject(int64_t segment_id, const std::u16string& plain_text,
                          bool block);
  void AppendBlockBoundary(int64_t segment_id, EditingBlockBoundaryEdge edge);
  void AppendSegment(int64_t segment_id, int64_t owner_id,
                     EditingSegmentKind kind, const std::u16string& text,
                     const std::u16string& plain_text, bool block,
                     EditingBlockBoundaryEdge edge);

  EditingNodePosition MapOffset(size_t offset,
                                EditingOffsetAffinity affinity) const;
  std::u16string SerializePlainText(const TextRange& range) const;

  const std::u16string& text() const { return text_; }
  const std::vector<EditingSegment>& segments() const { return segments_; }

 private:
  std::u16string text_;
  std::vector<EditingSegment> segments_;
};

}  // namespace lynx::editing

#endif  // CORE_RENDERER_EDITING_EDITING_PROJECTION_H_
