// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/edit_context_model.h"

namespace lynx::editing {

EditContextModel::EditContextModel(EditContextOptions options) {
  state_.text = std::move(options.text);
  if (options.selection_start <= state_.text.size() &&
      options.selection_end <= state_.text.size()) {
    state_.selection =
        TextRange(options.selection_start, options.selection_end);
  }
}

EditingStateSnapshot EditContextModel::GetSnapshot() const {
  EditingStateSnapshot result = state_;
  result.has_composition = composition_.has_value();
  result.composition = composition_.value_or(TextRange());
  return result;
}

bool EditContextModel::UpdateText(size_t range_start, size_t range_end,
                                  const std::u16string& text) {
  TextRange range(range_start, range_end);
  if (range.reversed() || !IsValidRange(range, state_.text.size())) {
    return false;
  }
  state_.text.replace(range.start(), range.length(), text);
  const size_t new_length = state_.text.size();
  state_.selection = TextRange(std::min(state_.selection.base(), new_length),
                               std::min(state_.selection.extent(), new_length));
  if (composition_ && !IsValidRange(*composition_, new_length)) {
    composition_.reset();
  }
  CommitState();
  return true;
}

bool EditContextModel::UpdateSelection(size_t start, size_t end) {
  TextRange selection(start, end);
  if (!IsValidRange(selection, state_.text.size())) {
    return false;
  }
  state_.selection = selection;
  CommitState();
  return true;
}

bool EditContextModel::ApplyNativeTextUpdate(
    size_t range_start, size_t range_end, const std::u16string& text,
    TextRange selection, std::optional<TextRange> composition) {
  TextRange update_range(range_start, range_end);
  if (update_range.reversed() ||
      !IsValidRange(update_range, state_.text.size())) {
    return false;
  }
  const size_t resulting_length =
      state_.text.size() - update_range.length() + text.size();
  if (!IsValidRange(selection, resulting_length) ||
      (composition && !IsValidRange(*composition, resulting_length))) {
    return false;
  }

  const bool started_composition = !composition_ && composition;
  const bool ended_composition = composition_ && !composition;
  if (started_composition) {
    Dispatch({EditContextEventType::kCompositionStart});
  }
  state_.text.replace(update_range.start(), update_range.length(), text);
  state_.selection = selection;
  composition_ = composition;
  CommitState();
  Dispatch({EditContextEventType::kTextUpdate, update_range, text, selection,
            composition});
  if (ended_composition) {
    Dispatch({EditContextEventType::kCompositionEnd});
  }
  return true;
}

bool EditContextModel::ApplyNativeSelection(
    TextRange selection, std::optional<TextRange> composition) {
  if (!IsValidRange(selection, state_.text.size()) ||
      (composition && !IsValidRange(*composition, state_.text.size()))) {
    return false;
  }
  const bool started_composition = !composition_ && composition;
  const bool ended_composition = composition_ && !composition;
  if (started_composition) {
    Dispatch({EditContextEventType::kCompositionStart});
  }
  state_.selection = selection;
  composition_ = composition;
  CommitState();
  if (ended_composition) {
    Dispatch({EditContextEventType::kCompositionEnd});
  }
  return true;
}

bool EditContextModel::IsValidRange(const TextRange& range,
                                    size_t length) const {
  return range.base() <= length && range.extent() <= length;
}

void EditContextModel::CommitState() {
  ++state_.revision;
  if (state_change_callback_) {
    state_change_callback_(GetSnapshot());
  }
}

void EditContextModel::Dispatch(EditContextEvent event) const {
  if (event_callback_) {
    event_callback_(event);
  }
}

}  // namespace lynx::editing
