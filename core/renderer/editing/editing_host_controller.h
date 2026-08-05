// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_EDITING_EDITING_HOST_CONTROLLER_H_
#define CORE_RENDERER_EDITING_EDITING_HOST_CONTROLLER_H_

#include <functional>
#include <optional>
#include <string>

#include "core/renderer/editing/edit_context_model.h"
#include "core/renderer/editing/editing_platform_contract.h"
#include "core/renderer/editing/editing_projection.h"

namespace lynx::editing {

struct EditingInputEvent {
  std::string input_type;
  std::u16string data;
  TextRange target_range;
};

class EditingHostController final : public EditingPlatformSession {
 public:
  explicit EditingHostController(EditContextOptions options);

  void Attach() override;
  void Detach() override;
  bool attached() const { return attached_; }

  void SetProjection(EditingProjection projection,
                     uint64_t projection_revision);
  const EditingProjection& projection() const { return projection_; }
  uint64_t projection_revision() const { return projection_revision_; }

  void SetBeforeInputCallback(
      std::function<bool(const EditingInputEvent&)> callback) {
    before_input_callback_ = std::move(callback);
  }
  void SetSelectionChangeCallback(
      std::function<void(const TextRange&)> callback) {
    selection_change_callback_ = std::move(callback);
  }

  EditContextModel& edit_context() { return edit_context_; }
  const EditContextModel& edit_context() const { return edit_context_; }

  // EditingPlatformSession
  void SetDelegate(EditingPlatformDelegate* delegate) override;
  bool Activate() override;
  void Deactivate() override;
  bool IsActive() const override { return attached_ && active_; }
  EditingStateSnapshot Snapshot() const override;
  EditingProjectionSnapshot ProjectionSnapshot() const override;
  EditingPlatformResult ApplyTransaction(
      const NativeTextTransaction& transaction) override;
  EditingPlatformResult PerformInput(const std::string& input_type,
                                     const std::u16string& data,
                                     uint64_t expected_revision) override;
  EditingPlatformResult SetSelection(TextRange selection,
                                     uint64_t expected_revision) override;
  EditingOperationStatus UpdateGeometry(
      EditingGeometrySnapshot geometry) override;
  EditingPlatformResult SetSelectionFromPoint(
      EditingLayoutPoint point, std::optional<size_t> anchor,
      uint64_t expected_revision) override;
  EditingSelectionRectsResult QuerySelectionRects(
      TextRange selection, uint64_t expected_revision) override;

 private:
  EditingPlatformResult Result(EditingOperationStatus status,
                               bool restart_input = false) const;
  void HandleStateChanged(const EditingStateSnapshot& snapshot);
  void NotifySelectionIfChanged(const TextRange& previous);
  std::optional<size_t> HitTest(EditingLayoutPoint point) const;
  static size_t PreviousGraphemeBoundary(const std::u16string& text,
                                         size_t offset);
  static size_t NextGraphemeBoundary(const std::u16string& text, size_t offset);

  EditContextModel edit_context_;
  EditingProjection projection_;
  uint64_t projection_revision_{0};
  std::optional<EditingGeometrySnapshot> geometry_;
  EditingPlatformDelegate* delegate_{nullptr};
  bool attached_{false};
  bool active_{false};
  std::function<bool(const EditingInputEvent&)> before_input_callback_;
  std::function<void(const TextRange&)> selection_change_callback_;
};

}  // namespace lynx::editing

#endif  // CORE_RENDERER_EDITING_EDITING_HOST_CONTROLLER_H_
