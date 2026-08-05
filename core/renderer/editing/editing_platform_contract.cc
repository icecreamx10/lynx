// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/editing_platform_contract.h"

#include <algorithm>
#include <cmath>

namespace lynx::editing {

namespace {

bool IsRangeValid(const TextRange& range, size_t length) {
  return range.base() <= length && range.extent() <= length;
}

}  // namespace

bool EditingGeometrySnapshot::Covers(const TextRange& range) const {
  return IsRangeValid(range, projection_length) && coverage.Contains(range);
}

bool EditingGeometrySnapshot::IsStructurallyValid() const {
  if (coverage.reversed() || !IsRangeValid(coverage, projection_length) ||
      !std::isfinite(control_bounds.x) || !std::isfinite(control_bounds.y) ||
      !std::isfinite(control_bounds.width) ||
      !std::isfinite(control_bounds.height) || control_bounds.width < 0 ||
      control_bounds.height < 0) {
    return false;
  }
  size_t previous_offset = 0;
  bool first = true;
  for (const EditingLayoutUnit& unit : units) {
    if (unit.projection_offset >= projection_length ||
        !coverage.Contains(
            TextRange(unit.projection_offset, unit.projection_offset + 1)) ||
        (!first && unit.projection_offset <= previous_offset) ||
        !std::isfinite(unit.bounds.x) || !std::isfinite(unit.bounds.y) ||
        !std::isfinite(unit.bounds.width) ||
        !std::isfinite(unit.bounds.height) || unit.bounds.width < 0 ||
        unit.bounds.height < 0 || unit.local_start > unit.local_end) {
      return false;
    }
    previous_offset = unit.projection_offset;
    first = false;
  }
  return true;
}

EditingOperationStatus ValidateTransaction(
    const EditingStateSnapshot& snapshot,
    const NativeTextTransaction& transaction) {
  if (transaction.expected_revision != snapshot.revision) {
    return EditingOperationStatus::kStaleRevision;
  }
  if (transaction.updates_text &&
      (transaction.replacement_range.reversed() ||
       !IsRangeValid(transaction.replacement_range, snapshot.text.size()))) {
    return EditingOperationStatus::kInvalidRange;
  }
  const size_t resulting_length =
      transaction.updates_text
          ? snapshot.text.size() - transaction.replacement_range.length() +
                transaction.replacement_text.size()
          : snapshot.text.size();
  if (!IsRangeValid(transaction.selection, resulting_length) ||
      (transaction.composition &&
       !IsRangeValid(*transaction.composition, resulting_length))) {
    return EditingOperationStatus::kInvalidRange;
  }
  return EditingOperationStatus::kAccepted;
}

}  // namespace lynx::editing
