// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/renderer/editing/editing_host_controller.h"
#include "core/renderer/editing/editing_host_registry.h"

namespace lynx::editing {
namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "EditContext smoke failed: " << message << '\n';
    std::exit(1);
  }
}

class SmokePlatform final : public EditingPlatformDelegate {
 public:
  explicit SmokePlatform(std::shared_ptr<EditingPlatformSession> session)
      : session_(std::move(session)) {}

  ~SmokePlatform() override { Detach(); }

  void PostLifecycle(EditingHostLifecycleEvent event) {
    pending_.push_back([this, event]() {
      switch (event) {
        case EditingHostLifecycleEvent::kAttached:
          session_->Attach();
          session_->SetDelegate(this);
          attached_ = true;
          break;
        case EditingHostLifecycleEvent::kActivated:
          Require(session_->Activate(), "platform activation was rejected");
          break;
        case EditingHostLifecycleEvent::kDeactivated:
          session_->Deactivate();
          break;
        case EditingHostLifecycleEvent::kDetached:
          Detach();
          break;
      }
    });
  }

  void Drain() {
    std::vector<std::function<void()>> work;
    work.swap(pending_);
    for (auto& task : work) {
      task();
    }
  }

  void OnStateChanged(const EditingStateUpdate& update) override {
    state_updates.push_back(update);
  }

  void OnActivationChanged(bool active) override {
    activations.push_back(active);
  }

  void OnGeometryRequested(const TextRange& range, uint64_t state_revision,
                           uint64_t projection_revision) override {
    geometry_requests.push_back(range);
    requested_state_revision = state_revision;
    requested_projection_revision = projection_revision;
  }

  void Detach() {
    if (!attached_) {
      return;
    }
    session_->Deactivate();
    session_->SetDelegate(nullptr);
    session_->Detach();
    attached_ = false;
  }

  std::vector<EditingStateUpdate> state_updates;
  std::vector<bool> activations;
  std::vector<TextRange> geometry_requests;
  uint64_t requested_state_revision{0};
  uint64_t requested_projection_revision{0};

 private:
  std::shared_ptr<EditingPlatformSession> session_;
  std::vector<std::function<void()>> pending_;
  bool attached_{false};
};

EditingGeometrySnapshot GeometryForText(uint64_t state_revision,
                                        uint64_t projection_revision,
                                        size_t length) {
  EditingGeometrySnapshot geometry;
  geometry.state_revision = state_revision;
  geometry.projection_revision = projection_revision;
  geometry.projection_length = length;
  geometry.coverage = TextRange(0, length);
  geometry.control_bounds = {0, 0, static_cast<float>(length * 10), 20};
  for (size_t offset = 0; offset < length; ++offset) {
    geometry.units.push_back({offset,
                              10,
                              20,
                              offset,
                              offset + 1,
                              {static_cast<float>(offset * 10), 0, 10, 20}});
  }
  return geometry;
}

void RunEditingSessionSmoke() {
  EditContextOptions options;
  options.text = u"A\U0001F600B";
  options.selection_start = 3;
  options.selection_end = 3;
  auto controller = std::make_shared<EditingHostController>(std::move(options));
  std::vector<EditContextEventType> events;
  controller->edit_context().SetEventCallback(
      [&events](const EditContextEvent& event) {
        events.push_back(event.type);
      });

  EditingProjection initial_projection;
  initial_projection.AppendText(20, u"A\U0001F600B", 10);
  controller->SetProjection(std::move(initial_projection), 1);

  EditingHostRegistry registry;
  SmokePlatform platform(controller);
  registry.AddLifecycleObserver(
      [&platform](int64_t host_id, EditingHostLifecycleEvent event) {
        Require(host_id == 7, "registry delivered the wrong host id");
        platform.PostLifecycle(event);
      });

  Require(registry.Attach(7, controller), "host attach failed");
  platform.Drain();
  Require(controller->attached(), "platform did not attach the session");
  Require(registry.Activate(7), "host activation failed");
  platform.Drain();
  Require(controller->IsActive(), "session is not active");

  EditingPlatformResult deleted =
      controller->PerformInput("deleteContentBackward", u"", 0);
  Require(deleted.accepted(), "surrogate-pair deletion was rejected");
  Require(deleted.snapshot.text == u"AB", "surrogate pair was split");
  Require(deleted.snapshot.selection == TextRange(1),
          "caret after deletion is wrong");

  EditingPlatformResult stale = controller->PerformInput("insertText", u"?", 0);
  Require(stale.status == EditingOperationStatus::kStaleRevision,
          "stale mutation was not rejected");
  Require(stale.restart_input, "stale mutation did not request native restart");
  Require(stale.snapshot.text == u"AB", "stale mutation changed text");

  NativeTextTransaction start_composition{
      "insertCompositionText", true, TextRange(1), u"xy", TextRange(3),
      TextRange(1, 3),         1};
  EditingPlatformResult composing =
      controller->ApplyTransaction(start_composition);
  Require(composing.accepted() && composing.snapshot.has_composition,
          "composition start failed");
  Require(composing.snapshot.text == u"AxyB", "composition text is wrong");

  NativeTextTransaction update_composition{
      "insertCompositionText", true, TextRange(1, 3), u"z", TextRange(2),
      TextRange(1, 2),         2};
  composing = controller->ApplyTransaction(update_composition);
  Require(composing.accepted() && composing.snapshot.text == u"AzB",
          "composition update failed");

  NativeTextTransaction end_composition{"insertFromComposition",
                                        false,
                                        TextRange(),
                                        u"",
                                        TextRange(2),
                                        std::nullopt,
                                        3};
  EditingPlatformResult committed =
      controller->ApplyTransaction(end_composition);
  Require(committed.accepted() && !committed.snapshot.has_composition,
          "composition commit failed");
  Require(events == (std::vector<EditContextEventType>{
                        EditContextEventType::kTextUpdate,
                        EditContextEventType::kCompositionStart,
                        EditContextEventType::kTextUpdate,
                        EditContextEventType::kTextUpdate,
                        EditContextEventType::kCompositionEnd}),
          "EditContext event order is wrong");

  EditingPlatformResult reversed = controller->SetSelection(TextRange(3, 0), 4);
  Require(reversed.accepted(), "directed selection was rejected");
  Require(reversed.snapshot.selection == TextRange(3, 0),
          "selection direction was lost");

  EditingProjection projection;
  projection.AppendText(20, u"AzB", 10);
  controller->SetProjection(std::move(projection), 2);
  EditingOperationStatus geometry_status = controller->UpdateGeometry(
      GeometryForText(reversed.snapshot.revision, 2, 3));
  Require(geometry_status == EditingOperationStatus::kAccepted,
          "valid geometry was rejected");

  EditingSelectionRectsResult rects =
      controller->QuerySelectionRects(TextRange(0, 3), 5);
  Require(rects.status == EditingOperationStatus::kAccepted &&
              rects.rects.size() == 3,
          "selection geometry is wrong");

  EditingPlatformResult hit =
      controller->SetSelectionFromPoint({16, 10}, std::nullopt, 5);
  Require(hit.accepted() && hit.snapshot.selection.collapsed(),
          "geometry hit test failed");

  controller->SetBeforeInputCallback(
      [](const EditingInputEvent&) { return false; });
  const EditingStateSnapshot before_cancel = controller->Snapshot();
  EditingPlatformResult cancelled = controller->PerformInput(
      "insertText", u"cancelled", before_cancel.revision);
  Require(cancelled.accepted(), "cancelled beforeinput was reported as error");
  Require(cancelled.snapshot.text == before_cancel.text &&
              cancelled.snapshot.revision == before_cancel.revision,
          "cancelled beforeinput mutated state");

  registry.Deactivate(7);
  platform.Drain();
  Require(!controller->IsActive(), "session did not deactivate");
  EditingPlatformResult inactive = controller->PerformInput(
      "insertText", u"x", controller->Snapshot().revision);
  Require(inactive.status == EditingOperationStatus::kInactive,
          "inactive input was not rejected");

  registry.Detach(7);
  platform.Drain();
  Require(!controller->attached(), "session did not detach");
  Require(platform.activations == (std::vector<bool>{true, false}),
          "activation callbacks are wrong");
}

void RunEmptyDocumentCaretSmoke() {
  auto controller =
      std::make_shared<EditingHostController>(EditContextOptions{});
  controller->Attach();
  Require(controller->Activate(), "empty session activation failed");
  controller->SetProjection(EditingProjection(), 1);
  EditingGeometrySnapshot geometry = GeometryForText(0, 1, 0);
  geometry.control_bounds = {4, 5, 80, 18};
  Require(controller->UpdateGeometry(std::move(geometry)) ==
              EditingOperationStatus::kAccepted,
          "empty geometry was rejected");
  EditingSelectionRectsResult caret =
      controller->QuerySelectionRects(TextRange(0), 0);
  Require(caret.status == EditingOperationStatus::kAccepted &&
              caret.rects.size() == 1 && caret.rects[0].x == 4 &&
              caret.rects[0].width == 0 && caret.rects[0].height == 18,
          "empty-document caret is wrong");
  controller->Detach();
}

}  // namespace
}  // namespace lynx::editing

int main() {
  lynx::editing::RunEditingSessionSmoke();
  lynx::editing::RunEmptyDocumentCaretSmoke();
  std::cout << "EditContext contract smoke passed\n";
  return 0;
}
