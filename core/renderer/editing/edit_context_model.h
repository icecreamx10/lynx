// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_EDITING_EDIT_CONTEXT_MODEL_H_
#define CORE_RENDERER_EDITING_EDIT_CONTEXT_MODEL_H_

#include <functional>
#include <optional>
#include <string>

#include "core/renderer/editing/editing_types.h"

namespace lynx::editing {

struct EditContextOptions {
  std::u16string text;
  size_t selection_start{0};
  size_t selection_end{0};
};

enum class EditContextEventType : uint8_t {
  kTextUpdate,
  kTextFormatUpdate,
  kCharacterBoundsUpdate,
  kCompositionStart,
  kCompositionEnd,
};

struct EditContextEvent {
  EditContextEventType type{EditContextEventType::kTextUpdate};
  TextRange update_range;
  std::u16string text;
  TextRange selection;
  std::optional<TextRange> composition;
};

class EditContextModel {
 public:
  explicit EditContextModel(EditContextOptions options);

  const std::u16string& text() const { return state_.text; }
  const TextRange& selection() const { return state_.selection; }
  const std::optional<TextRange>& composition() const { return composition_; }
  uint64_t revision() const { return state_.revision; }
  EditingStateSnapshot GetSnapshot() const;

  bool UpdateText(size_t range_start, size_t range_end,
                  const std::u16string& text);
  bool UpdateSelection(size_t start, size_t end);
  bool ApplyNativeTextUpdate(size_t range_start, size_t range_end,
                             const std::u16string& text, TextRange selection,
                             std::optional<TextRange> composition);
  bool ApplyNativeSelection(TextRange selection,
                            std::optional<TextRange> composition);

  void SetEventCallback(std::function<void(const EditContextEvent&)> callback) {
    event_callback_ = std::move(callback);
  }
  void SetStateChangeCallback(
      std::function<void(const EditingStateSnapshot&)> callback) {
    state_change_callback_ = std::move(callback);
  }

 private:
  bool IsValidRange(const TextRange& range, size_t length) const;
  void CommitState();
  void Dispatch(EditContextEvent event) const;

  EditingStateSnapshot state_;
  std::optional<TextRange> composition_;
  std::function<void(const EditContextEvent&)> event_callback_;
  std::function<void(const EditingStateSnapshot&)> state_change_callback_;
};

}  // namespace lynx::editing

#endif  // CORE_RENDERER_EDITING_EDIT_CONTEXT_MODEL_H_
