// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CLAY_LYNX_ADAPTOR_TEXT_EDIT_CONTEXT_SESSION_CLAY_H_
#define CLAY_LYNX_ADAPTOR_TEXT_EDIT_CONTEXT_SESSION_CLAY_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "base/include/fml/memory/weak_ptr.h"
#include "clay/lynx_adaptor/text_edit_context_geometry_clay.h"
#include "clay/ui/component/editable/text_input_controller.h"
#include "core/renderer/editing/editing_platform_contract.h"

namespace clay {
class FloatPoint;
class TextView;
}  // namespace clay

namespace lynx::tasm {

// Adapts Clay's desktop text-input channel (NSTextInputClient on macOS) to
// the shared EditContext session. It deliberately owns no editing semantics.
class TextEditContextSessionClay final
    : public editing::EditingPlatformDelegate,
      public clay::TextInputClient {
 public:
  TextEditContextSessionClay(
      clay::TextView* host,
      std::shared_ptr<editing::EditingPlatformSession> session);
  ~TextEditContextSessionClay() override;

  void Activate();
  void Deactivate();
  void ViewTreeDidChange();

  // EditingPlatformDelegate
  void OnStateChanged(const editing::EditingStateUpdate& update) override;
  void OnActivationChanged(bool active) override;
  void OnGeometryRequested(const editing::TextRange& range,
                           uint64_t state_revision,
                           uint64_t projection_revision) override;

  // clay::TextInputClient
  void UpdateEditingState(std::string text, clay::TextSelection selection,
                          clay::TextRange composing,
                          clay::Affinity affinity) override;
  void PerformAction() override;

 private:
  void ApplySnapshot(const editing::EditingStateSnapshot& snapshot,
                     bool restart_input, bool refresh_geometry);
  void PushNativeState(const editing::EditingStateSnapshot& snapshot,
                       bool restart_input, bool query_caret_geometry);
  void RefreshGeometry(
      editing::TextRange requested, uint64_t state_revision,
      uint64_t projection_revision,
      std::optional<editing::EditingLayoutPoint> point = std::nullopt);
  std::vector<ClayEditingMeasuredUnit> BuildLayoutUnits(
      const editing::EditingProjectionSnapshot& projection) const;
  void RefreshTextPlacements(
      const editing::EditingProjectionSnapshot& projection);
  void RefreshSelectionCallbacks(
      const editing::EditingProjectionSnapshot& projection);
  void UpdateRenderedSelection(
      const editing::EditingStateSnapshot& snapshot,
      const editing::EditingProjectionSnapshot& projection);
  void HandleViewSelectionChanged(int64_t owner_id, int start, int end);
  void HandlePointerSelection(const clay::FloatPoint& point, bool extend);
  std::optional<size_t> MapViewOffsetToProjection(int64_t owner_id,
                                                  size_t offset) const;

  clay::TextView* host_{nullptr};
  std::shared_ptr<editing::EditingPlatformSession> session_;
  std::unique_ptr<clay::TextInputController> text_input_controller_;
  editing::EditingStateSnapshot snapshot_;
  editing::EditingProjectionSnapshot projection_;
  std::vector<ClayEditingTextPlacement> text_placements_;
  std::unordered_set<int64_t> callback_owner_ids_;
  std::optional<size_t> pointer_anchor_;
  bool applying_snapshot_{false};
  bool active_{false};
  fml::WeakPtrFactory<TextEditContextSessionClay> weak_factory_;
};

}  // namespace lynx::tasm

#endif  // CLAY_LYNX_ADAPTOR_TEXT_EDIT_CONTEXT_SESSION_CLAY_H_
