// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_EDITING_EDITING_TYPES_H_
#define CORE_RENDERER_EDITING_EDITING_TYPES_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lynx::editing {

struct TextRange {
  TextRange() = default;
  explicit TextRange(size_t position) : base_(position), extent_(position) {}
  TextRange(size_t base, size_t extent) : base_(base), extent_(extent) {}

  size_t base() const { return base_; }
  size_t extent() const { return extent_; }
  size_t start() const { return std::min(base_, extent_); }
  size_t end() const { return std::max(base_, extent_); }
  size_t length() const { return end() - start(); }
  size_t position() const { return extent_; }
  bool collapsed() const { return base_ == extent_; }
  bool reversed() const { return base_ > extent_; }
  bool Contains(const TextRange& other) const {
    return start() <= other.start() && end() >= other.end();
  }
  bool operator==(const TextRange& other) const {
    return base_ == other.base_ && extent_ == other.extent_;
  }

 private:
  size_t base_{0};
  size_t extent_{0};
};

struct EditContextRect {
  float x{0};
  float y{0};
  float width{0};
  float height{0};
};

enum class EditingSegmentKind : uint8_t {
  kText = 0,
  kBlockBoundary = 1,
  kAtomicObject = 2,
};

enum class EditingBlockBoundaryEdge : uint8_t {
  kNone = 0,
  kLeading = 1,
  kTrailing = 2,
};

enum class EditingOffsetAffinity : uint8_t {
  kDownstream,
  kUpstream,
};

struct EditingNodePosition {
  int64_t owner_id{-1};
  size_t offset{0};
  bool valid{false};
};

struct EditingSegment {
  int64_t segment_id{-1};
  int64_t owner_id{-1};
  EditingSegmentKind kind{EditingSegmentKind::kText};
  std::u16string text;
  std::u16string plain_text;
  size_t start{0};
  size_t end{0};
  bool block{false};
  EditingBlockBoundaryEdge boundary_edge{EditingBlockBoundaryEdge::kNone};

  bool Contains(size_t offset) const { return start <= offset && offset < end; }
};

struct EditingStateSnapshot {
  std::u16string text;
  TextRange selection;
  TextRange composition;
  bool has_composition{false};
  uint64_t revision{0};
};

struct EditingOperationResult {
  bool accepted{false};
  bool restart_input{false};
  EditingStateSnapshot snapshot;
};

enum class EditingHostLifecycleEvent : uint8_t {
  kAttached,
  kDetached,
  kActivated,
  kDeactivated,
};

}  // namespace lynx::editing

#endif  // CORE_RENDERER_EDITING_EDITING_TYPES_H_
