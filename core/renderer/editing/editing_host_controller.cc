// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/editing_host_controller.h"

#include <algorithm>
#include <limits>

namespace lynx::editing {

namespace {

bool IsHighSurrogate(char16_t value) {
  return value >= 0xD800 && value <= 0xDBFF;
}

bool IsLowSurrogate(char16_t value) {
  return value >= 0xDC00 && value <= 0xDFFF;
}

bool Contains(const EditContextRect& rect, EditingLayoutPoint point) {
  return point.x >= rect.x && point.x <= rect.x + rect.width &&
         point.y >= rect.y && point.y <= rect.y + rect.height;
}

float DistanceSquared(const EditContextRect& rect, EditingLayoutPoint point) {
  const float center_x = rect.x + rect.width * 0.5f;
  const float center_y = rect.y + rect.height * 0.5f;
  const float dx = center_x - point.x;
  const float dy = center_y - point.y;
  return dx * dx + dy * dy;
}

}  // namespace

EditingHostController::EditingHostController(EditContextOptions options)
    : edit_context_(std::move(options)) {
  edit_context_.SetStateChangeCallback(
      [this](const EditingStateSnapshot& snapshot) {
        HandleStateChanged(snapshot);
      });
}

void EditingHostController::Attach() { attached_ = true; }

void EditingHostController::Detach() {
  Deactivate();
  attached_ = false;
  delegate_ = nullptr;
  geometry_.reset();
}

void EditingHostController::SetProjection(EditingProjection projection,
                                          uint64_t projection_revision) {
  projection_ = std::move(projection);
  projection_revision_ = projection_revision;
  geometry_.reset();
  if (delegate_ && active_) {
    delegate_->OnGeometryRequested(TextRange(0, projection_.text().size()),
                                   edit_context_.revision(),
                                   projection_revision_);
  }
}

void EditingHostController::SetDelegate(EditingPlatformDelegate* delegate) {
  delegate_ = delegate;
}

bool EditingHostController::Activate() {
  if (!attached_) {
    return false;
  }
  if (!active_) {
    active_ = true;
    if (delegate_) {
      delegate_->OnActivationChanged(true);
      delegate_->OnStateChanged(
          {Snapshot(), EditingStateChange::kActivation, true});
    }
  }
  return true;
}

void EditingHostController::Deactivate() {
  if (!active_) {
    return;
  }
  active_ = false;
  if (delegate_) {
    delegate_->OnActivationChanged(false);
  }
}

EditingStateSnapshot EditingHostController::Snapshot() const {
  return edit_context_.GetSnapshot();
}

EditingProjectionSnapshot EditingHostController::ProjectionSnapshot() const {
  return {projection_revision_, projection_.text().size(),
          projection_.segments()};
}

EditingPlatformResult EditingHostController::ApplyTransaction(
    const NativeTextTransaction& transaction) {
  if (!IsActive()) {
    return Result(EditingOperationStatus::kInactive);
  }
  EditingOperationStatus validation =
      ValidateTransaction(Snapshot(), transaction);
  if (validation != EditingOperationStatus::kAccepted) {
    return Result(validation,
                  validation == EditingOperationStatus::kStaleRevision);
  }
  EditingInputEvent event{transaction.input_type, transaction.replacement_text,
                          transaction.replacement_range};
  if (before_input_callback_ && !before_input_callback_(event)) {
    return Result(EditingOperationStatus::kAccepted);
  }

  const TextRange previous_selection = edit_context_.selection();
  const bool applied =
      transaction.updates_text
          ? edit_context_.ApplyNativeTextUpdate(
                transaction.replacement_range.start(),
                transaction.replacement_range.end(),
                transaction.replacement_text, transaction.selection,
                transaction.composition)
          : edit_context_.ApplyNativeSelection(transaction.selection,
                                               transaction.composition);
  if (!applied) {
    return Result(EditingOperationStatus::kInvalidRange);
  }
  NotifySelectionIfChanged(previous_selection);
  return Result(EditingOperationStatus::kAccepted);
}

EditingPlatformResult EditingHostController::PerformInput(
    const std::string& input_type, const std::u16string& data,
    uint64_t expected_revision) {
  if (!IsActive()) {
    return Result(EditingOperationStatus::kInactive);
  }
  EditingStateSnapshot state = Snapshot();
  if (expected_revision != state.revision) {
    return Result(EditingOperationStatus::kStaleRevision, true);
  }
  TextRange replacement =
      state.has_composition ? state.composition : state.selection;
  std::u16string replacement_text;
  if (input_type.rfind("insert", 0) == 0) {
    replacement_text = data;
  } else if (input_type == "deleteContentBackward") {
    if (replacement.collapsed()) {
      replacement =
          TextRange(PreviousGraphemeBoundary(state.text, replacement.start()),
                    replacement.start());
    }
  } else if (input_type == "deleteContentForward") {
    if (replacement.collapsed()) {
      replacement =
          TextRange(replacement.start(),
                    NextGraphemeBoundary(state.text, replacement.start()));
    }
  } else if (input_type.rfind("delete", 0) != 0) {
    return Result(EditingOperationStatus::kUnsupportedInputType);
  }
  const size_t caret = replacement.start() + replacement_text.size();
  NativeTextTransaction transaction{
      input_type,       true,         replacement,      replacement_text,
      TextRange(caret), std::nullopt, expected_revision};
  return ApplyTransaction(transaction);
}

EditingPlatformResult EditingHostController::SetSelection(
    TextRange selection, uint64_t expected_revision) {
  EditingStateSnapshot state = Snapshot();
  NativeTextTransaction transaction{
      "",
      false,
      TextRange(),
      u"",
      selection,
      state.has_composition ? std::optional<TextRange>(state.composition)
                            : std::nullopt,
      expected_revision};
  return ApplyTransaction(transaction);
}

EditingOperationStatus EditingHostController::UpdateGeometry(
    EditingGeometrySnapshot geometry) {
  if (!IsActive()) {
    return EditingOperationStatus::kInactive;
  }
  if (geometry.state_revision != edit_context_.revision() ||
      geometry.projection_revision != projection_revision_) {
    return EditingOperationStatus::kStaleRevision;
  }
  if (geometry.projection_length != projection_.text().size() ||
      !geometry.IsStructurallyValid()) {
    return EditingOperationStatus::kInvalidRange;
  }
  geometry_ = std::move(geometry);
  return EditingOperationStatus::kAccepted;
}

EditingPlatformResult EditingHostController::SetSelectionFromPoint(
    EditingLayoutPoint point, std::optional<size_t> anchor,
    uint64_t expected_revision) {
  if (!IsActive()) {
    return Result(EditingOperationStatus::kInactive);
  }
  if (expected_revision != edit_context_.revision()) {
    return Result(EditingOperationStatus::kStaleRevision, true);
  }
  std::optional<size_t> offset = HitTest(point);
  if (!offset) {
    if (delegate_) {
      delegate_->OnGeometryRequested(TextRange(0, projection_.text().size()),
                                     edit_context_.revision(),
                                     projection_revision_);
    }
    return Result(EditingOperationStatus::kGeometryUnavailable);
  }
  return SetSelection(TextRange(anchor.value_or(*offset), *offset),
                      expected_revision);
}

EditingSelectionRectsResult EditingHostController::QuerySelectionRects(
    TextRange selection, uint64_t expected_revision) {
  if (!IsActive()) {
    return {EditingOperationStatus::kInactive, {}};
  }
  if (expected_revision != edit_context_.revision()) {
    return {EditingOperationStatus::kStaleRevision, {}};
  }
  if (selection.base() > projection_.text().size() ||
      selection.extent() > projection_.text().size()) {
    return {EditingOperationStatus::kInvalidRange, {}};
  }
  if (!geometry_ || !geometry_->Covers(selection)) {
    if (delegate_) {
      delegate_->OnGeometryRequested(selection, edit_context_.revision(),
                                     projection_revision_);
    }
    return {EditingOperationStatus::kGeometryUnavailable, {}};
  }
  std::vector<EditContextRect> result;
  if (selection.collapsed()) {
    const size_t position = selection.position();
    if (projection_.text().empty() && position == 0) {
      EditContextRect caret = geometry_->control_bounds;
      caret.width = 0;
      return {EditingOperationStatus::kAccepted, {caret}};
    }
    for (const EditingLayoutUnit& unit : geometry_->units) {
      const bool at_leading_edge = unit.projection_offset == position;
      const bool at_trailing_edge = unit.projection_offset + 1 == position;
      if (!at_leading_edge && !at_trailing_edge) {
        continue;
      }
      EditContextRect caret = unit.bounds;
      const bool right_to_left =
          (static_cast<uint8_t>(unit.flags) &
           static_cast<uint8_t>(EditingLayoutUnitFlag::kRightToLeft)) != 0;
      const bool use_right_edge =
          at_leading_edge ? right_to_left : !right_to_left;
      caret.x += use_right_edge ? caret.width : 0;
      caret.width = 0;
      result.push_back(caret);
      break;
    }
    if (result.empty()) {
      if (delegate_) {
        delegate_->OnGeometryRequested(selection, edit_context_.revision(),
                                       projection_revision_);
      }
      return {EditingOperationStatus::kGeometryUnavailable, {}};
    }
    return {EditingOperationStatus::kAccepted, std::move(result)};
  }
  for (const EditingLayoutUnit& unit : geometry_->units) {
    if (unit.projection_offset >= selection.start() &&
        unit.projection_offset < selection.end()) {
      result.push_back(unit.bounds);
    }
  }
  return {EditingOperationStatus::kAccepted, std::move(result)};
}

EditingPlatformResult EditingHostController::Result(
    EditingOperationStatus status, bool restart_input) const {
  return {status, restart_input, Snapshot()};
}

void EditingHostController::HandleStateChanged(
    const EditingStateSnapshot& snapshot) {
  geometry_.reset();
  if (delegate_ && active_) {
    delegate_->OnStateChanged(
        {snapshot,
         EditingStateChange::kText | EditingStateChange::kSelection |
             EditingStateChange::kComposition | EditingStateChange::kGeometry,
         false});
  }
}

void EditingHostController::NotifySelectionIfChanged(
    const TextRange& previous) {
  if (selection_change_callback_ && !(previous == edit_context_.selection())) {
    selection_change_callback_(edit_context_.selection());
  }
}

std::optional<size_t> EditingHostController::HitTest(
    EditingLayoutPoint point) const {
  if (!geometry_ || geometry_->units.empty()) {
    return std::nullopt;
  }
  const EditingLayoutUnit* best = nullptr;
  float best_distance = std::numeric_limits<float>::max();
  for (const EditingLayoutUnit& unit : geometry_->units) {
    if (Contains(unit.bounds, point)) {
      best = &unit;
      break;
    }
    const float distance = DistanceSquared(unit.bounds, point);
    if (distance < best_distance) {
      best_distance = distance;
      best = &unit;
    }
  }
  if (!best) {
    return std::nullopt;
  }
  const bool after = point.x > best->bounds.x + best->bounds.width * 0.5f;
  return std::min(projection_.text().size(),
                  best->projection_offset + (after ? 1u : 0u));
}

size_t EditingHostController::PreviousGraphemeBoundary(
    const std::u16string& text, size_t offset) {
  if (offset == 0) {
    return 0;
  }
  size_t result = offset - 1;
  if (result > 0 && IsLowSurrogate(text[result]) &&
      IsHighSurrogate(text[result - 1])) {
    --result;
  }
  return result;
}

size_t EditingHostController::NextGraphemeBoundary(const std::u16string& text,
                                                   size_t offset) {
  if (offset >= text.size()) {
    return text.size();
  }
  size_t result = offset + 1;
  if (result < text.size() && IsHighSurrogate(text[offset]) &&
      IsLowSurrogate(text[result])) {
    ++result;
  }
  return result;
}

}  // namespace lynx::editing
