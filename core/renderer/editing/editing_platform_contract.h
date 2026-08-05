// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_EDITING_EDITING_PLATFORM_CONTRACT_H_
#define CORE_RENDERER_EDITING_EDITING_PLATFORM_CONTRACT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/renderer/editing/editing_types.h"

namespace lynx::editing {

// C++ owns all editing semantics. Platform adapters may only translate native
// input into these operations and render/query the returned state.
//
// Threading contract:
//   * Every method is called on the Lynx UI thread.
//   * Snapshot() is synchronous because native IMEs synchronously query text.
//   * Delegate callbacks are delivered on the same thread and must not reenter
//     the session. A platform needing async UI work must post it.
//
// Coordinate contract:
//   * Text offsets and ranges are UTF-16 code-unit offsets.
//   * Geometry is viewport-relative logical pixels.
//   * Geometry carries both state and projection revisions.
//   * Partial coverage is valid for virtualized hosts.

enum class EditingOperationStatus : uint8_t {
  kAccepted,
  kInactive,
  kStaleRevision,
  kInvalidRange,
  kUnsupportedInputType,
  kGeometryUnavailable,
};

enum class EditingStateChange : uint32_t {
  kNone = 0,
  kText = 1u << 0,
  kSelection = 1u << 1,
  kComposition = 1u << 2,
  kGeometry = 1u << 3,
  kActivation = 1u << 4,
};

constexpr EditingStateChange operator|(EditingStateChange lhs,
                                       EditingStateChange rhs) {
  return static_cast<EditingStateChange>(static_cast<uint32_t>(lhs) |
                                         static_cast<uint32_t>(rhs));
}

constexpr bool HasStateChange(EditingStateChange value,
                              EditingStateChange flag) {
  return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

struct NativeTextTransaction {
  std::string input_type;
  bool updates_text{false};
  TextRange replacement_range;
  std::u16string replacement_text;
  TextRange selection;
  std::optional<TextRange> composition;
  uint64_t expected_revision{0};
};

struct EditingPlatformResult {
  EditingOperationStatus status{EditingOperationStatus::kInactive};
  bool restart_input{false};
  EditingStateSnapshot snapshot;

  bool accepted() const { return status == EditingOperationStatus::kAccepted; }
};

struct EditingSelectionRectsResult {
  EditingOperationStatus status{EditingOperationStatus::kGeometryUnavailable};
  std::vector<EditContextRect> rects;
};

struct EditingLayoutPoint {
  float x{0};
  float y{0};
};

enum class EditingLayoutUnitFlag : uint8_t {
  kNone = 0,
  kHasDirection = 1u << 0,
  kRightToLeft = 1u << 1,
  kAtomic = 1u << 2,
  kBlock = 1u << 3,
};

struct EditingLayoutUnit {
  size_t projection_offset{0};
  int64_t segment_id{-1};
  int64_t owner_id{-1};
  size_t local_start{0};
  size_t local_end{0};
  EditContextRect bounds;
  EditingLayoutUnitFlag flags{EditingLayoutUnitFlag::kNone};
  EditingBlockBoundaryEdge boundary_edge{EditingBlockBoundaryEdge::kNone};
};

struct EditingGeometrySnapshot {
  uint64_t state_revision{0};
  uint64_t projection_revision{0};
  size_t projection_length{0};
  TextRange coverage;
  EditContextRect control_bounds;
  std::vector<EditingLayoutUnit> units;

  bool Covers(const TextRange& range) const;
  bool IsStructurallyValid() const;
};

struct EditingProjectionSnapshot {
  uint64_t revision{0};
  size_t length{0};
  std::vector<EditingSegment> segments;
};

struct EditingStateUpdate {
  EditingStateSnapshot snapshot;
  EditingStateChange changes{EditingStateChange::kNone};
  bool restart_input{false};
};

class EditingPlatformDelegate {
 public:
  virtual ~EditingPlatformDelegate() = default;

  virtual void OnStateChanged(const EditingStateUpdate& update) = 0;
  virtual void OnActivationChanged(bool active) = 0;
  virtual void OnGeometryRequested(const TextRange& range,
                                   uint64_t state_revision,
                                   uint64_t projection_revision) = 0;
};

// Implemented by the shared C++ controller and consumed by Android, Darwin,
// and desktop adapters. The platform must never mutate a cached snapshot.
class EditingPlatformSession {
 public:
  virtual ~EditingPlatformSession() = default;

  virtual void Attach() = 0;
  virtual void Detach() = 0;
  virtual void SetDelegate(EditingPlatformDelegate* delegate) = 0;
  virtual bool Activate() = 0;
  virtual void Deactivate() = 0;
  virtual bool IsActive() const = 0;
  virtual EditingStateSnapshot Snapshot() const = 0;
  virtual EditingProjectionSnapshot ProjectionSnapshot() const = 0;

  virtual EditingPlatformResult ApplyTransaction(
      const NativeTextTransaction& transaction) = 0;
  virtual EditingPlatformResult PerformInput(const std::string& input_type,
                                             const std::u16string& data,
                                             uint64_t expected_revision) = 0;
  virtual EditingPlatformResult SetSelection(TextRange selection,
                                             uint64_t expected_revision) = 0;

  virtual EditingOperationStatus UpdateGeometry(
      EditingGeometrySnapshot geometry) = 0;
  virtual EditingPlatformResult SetSelectionFromPoint(
      EditingLayoutPoint point, std::optional<size_t> anchor,
      uint64_t expected_revision) = 0;
  virtual EditingSelectionRectsResult QuerySelectionRects(
      TextRange selection, uint64_t expected_revision) = 0;

  // Convenience for native APIs that express unavailable geometry as an
  // empty list. QuerySelectionRects() remains the authoritative operation.
  std::vector<EditContextRect> SelectionRects(TextRange selection,
                                              uint64_t expected_revision) {
    return QuerySelectionRects(selection, expected_revision).rects;
  }
};

EditingOperationStatus ValidateTransaction(
    const EditingStateSnapshot& snapshot,
    const NativeTextTransaction& transaction);

}  // namespace lynx::editing

#endif  // CORE_RENDERER_EDITING_EDITING_PLATFORM_CONTRACT_H_
