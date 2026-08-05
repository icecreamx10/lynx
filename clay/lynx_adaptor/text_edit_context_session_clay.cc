// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "clay/lynx_adaptor/text_edit_context_session_clay.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/include/string/string_utils.h"
#include "clay/gfx/geometry/float_point.h"
#include "clay/gfx/geometry/float_rect.h"
#include "clay/gfx/geometry/transform.h"
#include "clay/ui/component/page_view.h"
#include "clay/ui/component/text/text_view.h"
#include "clay/ui/component/view_context.h"
#include "clay/ui/platform/keyboard_types.h"

namespace lynx::tasm {

namespace {

clay::TextView* FindTextView(clay::ViewContext* context, int64_t owner_id) {
  if (!context || owner_id < std::numeric_limits<int>::min() ||
      owner_id > std::numeric_limits<int>::max()) {
    return nullptr;
  }
  clay::BaseView* view = context->GetViewById(static_cast<int>(owner_id));
  return view && view->Is<clay::TextView>()
             ? static_cast<clay::TextView*>(view)
             : nullptr;
}

editing::EditContextRect ToEditRect(const clay::FloatRect& rect) {
  return {rect.x(), rect.y(), rect.width(), rect.height()};
}

}  // namespace

TextEditContextSessionClay::TextEditContextSessionClay(
    clay::TextView* host,
    std::shared_ptr<editing::EditingPlatformSession> session)
    : host_(host),
      session_(std::move(session)),
      weak_factory_(this) {
  if (!host_ || !session_) {
    return;
  }
  session_->Attach();
  text_input_controller_ = std::make_unique<clay::TextInputController>(
      host_->page_view(), host_->id(), this);
  text_input_controller_->SetMultiline(true);
  session_->SetDelegate(this);
  snapshot_ = session_->Snapshot();
  projection_ = session_->ProjectionSnapshot();
  RefreshSelectionCallbacks(projection_);
}

TextEditContextSessionClay::~TextEditContextSessionClay() {
  Deactivate();
  for (int64_t owner_id : callback_owner_ids_) {
    if (auto* view = FindTextView(host_ ? host_->page_view()->GetViewContext()
                                        : nullptr,
                                  owner_id)) {
      view->SetEditContextSelectionChangedCallback(nullptr);
      view->SetEditContextHitTestCallback(nullptr);
    }
  }
  if (session_) {
    session_->SetDelegate(nullptr);
    session_->Detach();
  }
}

void TextEditContextSessionClay::Activate() {
  if (!host_ || !session_ || !text_input_controller_ || active_) {
    return;
  }
  text_input_controller_->SetClient(host_->id(),
                                    clay::KeyboardAction::kMultiLine,
                                    clay::KeyboardInputType::kClassText);
  clay::Transform identity;
  identity.MakeIdentity();
  text_input_controller_->SetEditableTransform(identity);
  if (!session_->Activate()) {
    text_input_controller_->ClearClient();
    return;
  }
  active_ = true;
  snapshot_ = session_->Snapshot();
  projection_ = session_->ProjectionSnapshot();
  ApplySnapshot(snapshot_, true, true);
  text_input_controller_->Show();
}

void TextEditContextSessionClay::Deactivate() {
  if (!active_) {
    return;
  }
  active_ = false;
  pointer_anchor_.reset();
  text_input_controller_->Hide();
  text_input_controller_->ClearClient();
  if (session_ && session_->IsActive()) {
    session_->Deactivate();
  }
}

void TextEditContextSessionClay::OnStateChanged(
    const editing::EditingStateUpdate& update) {
  // Delegate callbacks are non-reentrant, so only update native/render caches
  // here. Geometry is refreshed after the initiating operation returns.
  ApplySnapshot(update.snapshot, update.restart_input, false);
}

void TextEditContextSessionClay::OnActivationChanged(bool active) {
  if (!text_input_controller_) {
    return;
  }
  if (active) {
    text_input_controller_->Show();
  } else {
    text_input_controller_->Hide();
  }
}

void TextEditContextSessionClay::OnGeometryRequested(
    const editing::TextRange& range, uint64_t state_revision,
    uint64_t projection_revision) {
  if (!host_) {
    return;
  }
  auto weak = weak_factory_.GetWeakPtr();
  host_->page_view()->GetTaskRunner()->PostTask(
      [weak, range, state_revision, projection_revision]() {
        if (weak) {
          weak->RefreshGeometry(range, state_revision, projection_revision);
        }
      });
}

void TextEditContextSessionClay::UpdateEditingState(
    std::string text, clay::TextSelection selection, clay::TextRange composing,
    clay::Affinity affinity) {
  if (!session_ || !active_ || applying_snapshot_ ||
      selection.base_offset() < 0 || selection.extent_offset() < 0) {
    return;
  }

  const std::u16string native_text = base::U8StringToU16(text);
  const editing::EditingStateSnapshot before = session_->Snapshot();
  size_t prefix = 0;
  while (prefix < before.text.size() && prefix < native_text.size() &&
         before.text[prefix] == native_text[prefix]) {
    ++prefix;
  }
  size_t old_suffix = before.text.size();
  size_t new_suffix = native_text.size();
  while (old_suffix > prefix && new_suffix > prefix &&
         before.text[old_suffix - 1] == native_text[new_suffix - 1]) {
    --old_suffix;
    --new_suffix;
  }

  editing::NativeTextTransaction transaction;
  transaction.expected_revision = before.revision;
  transaction.updates_text = before.text != native_text;
  transaction.replacement_range = editing::TextRange(prefix, old_suffix);
  transaction.replacement_text =
      native_text.substr(prefix, new_suffix - prefix);
  transaction.selection = editing::TextRange(
      static_cast<size_t>(selection.base_offset()),
      static_cast<size_t>(selection.extent_offset()));
  if (composing.extent() > composing.base()) {
    transaction.composition = editing::TextRange(
        static_cast<size_t>(composing.base()),
        static_cast<size_t>(composing.extent()));
  }
  if (!transaction.updates_text) {
    transaction.input_type = "insertText";
  } else if (!transaction.replacement_text.empty() &&
             transaction.replacement_range.length() == 0) {
    transaction.input_type = transaction.composition
                                 ? "insertCompositionText"
                                 : "insertText";
  } else if (transaction.replacement_text.empty()) {
    transaction.input_type = "deleteContentBackward";
  } else {
    transaction.input_type = "insertReplacementText";
  }

  const editing::EditingPlatformResult result =
      session_->ApplyTransaction(transaction);
  ApplySnapshot(result.snapshot, result.restart_input || !result.accepted(),
                result.accepted());
}

void TextEditContextSessionClay::PerformAction() {
  // AppKit delivers multiline return through insertText: before the action.
  // The frontend's beforeinput/default action remains owned by C++.
}

void TextEditContextSessionClay::ApplySnapshot(
    const editing::EditingStateSnapshot& snapshot, bool restart_input,
    bool refresh_geometry) {
  snapshot_ = snapshot;
  if (session_ && refresh_geometry) {
    projection_ = session_->ProjectionSnapshot();
  }
  RefreshSelectionCallbacks(projection_);
  PushNativeState(snapshot_, restart_input, refresh_geometry);
  UpdateRenderedSelection(snapshot_, projection_);
  if (refresh_geometry && session_) {
    RefreshGeometry(editing::TextRange(0, projection_.length),
                    snapshot_.revision, projection_.revision);
  }
}

void TextEditContextSessionClay::PushNativeState(
    const editing::EditingStateSnapshot& snapshot, bool restart_input,
    bool query_caret_geometry) {
  if (!text_input_controller_) {
    return;
  }
  clay::TextEditingValue value(
      base::U16StringToU8(snapshot.text),
      clay::TextRange(snapshot.selection.base(), snapshot.selection.extent()),
      clay::TextRange(snapshot.has_composition ? snapshot.composition.base() : 0,
                      snapshot.has_composition ? snapshot.composition.extent()
                                               : 0),
      snapshot.has_composition, clay::Affinity::kDownstream);
  text_input_controller_->SetEditingState(value);
  if (restart_input && active_) {
    text_input_controller_->ClearClient();
    text_input_controller_->SetClient(host_->id(),
                                      clay::KeyboardAction::kMultiLine,
                                      clay::KeyboardInputType::kClassText);
  }

  if (session_ && query_caret_geometry) {
    const auto rects =
        session_->SelectionRects(snapshot.selection, snapshot.revision);
    if (!rects.empty()) {
      const auto& caret = snapshot.selection.reversed() ? rects.front()
                                                        : rects.back();
      text_input_controller_->SetCaretRect(
          clay::FloatRect(caret.x, caret.y, caret.width, caret.height));
    }
  }
}

std::vector<TextEditContextSessionClay::MeasuredUnit>
TextEditContextSessionClay::BuildLayoutUnits(
    const editing::EditingProjectionSnapshot& projection) const {
  std::vector<MeasuredUnit> measured(projection.length);
  if (!host_) {
    return measured;
  }
  clay::ViewContext* context = host_->page_view()->GetViewContext();
  for (const editing::EditingSegment& segment : projection.segments) {
    if (segment.start >= segment.end || segment.end > projection.length) {
      continue;
    }
    if (segment.kind == editing::EditingSegmentKind::kAtomicObject) {
      clay::BaseView* atom =
          context && segment.segment_id >= std::numeric_limits<int>::min() &&
                  segment.segment_id <= std::numeric_limits<int>::max()
                                 ? context->GetViewById(
                                       static_cast<int>(segment.segment_id))
                                 : nullptr;
      if (!atom || segment.end != segment.start + 1) {
        continue;
      }
      auto& output = measured[segment.start];
      output.available = true;
      output.unit.projection_offset = segment.start;
      output.unit.segment_id = segment.segment_id;
      output.unit.owner_id = segment.owner_id;
      output.unit.local_start = 0;
      output.unit.local_end = 1;
      output.unit.bounds = ToEditRect(atom->BoundsRelativeTo(nullptr));
      output.unit.flags = editing::EditingLayoutUnitFlag::kAtomic;
      continue;
    }
    if (segment.kind != editing::EditingSegmentKind::kText) {
      continue;
    }
    clay::TextView* view = FindTextView(context, segment.owner_id);
    if (!view) {
      continue;
    }
    const auto& rendered_text = view->GetRenderText()->GetText();
    const size_t count = std::min(segment.end - segment.start,
                                  rendered_text.size());
    const clay::FloatRect view_bounds = view->BoundsRelativeTo(nullptr);
    const float origin_x = view_bounds.x() + view->BorderLeft() +
                           view->PaddingLeft();
    const float origin_y =
        view_bounds.y() + view->BorderTop() + view->PaddingTop();
    for (size_t local = 0; local < count; ++local) {
      const auto rects = view->GetRenderText()->GetTextLineRects(
          static_cast<int>(local), static_cast<int>(local + 1));
      if (rects.empty()) {
        continue;
      }
      clay::FloatRect bounds = rects.front();
      for (const auto& rect : rects) {
        bounds.ExpandToInclude(rect);
      }
      bounds.Move(origin_x, origin_y);
      auto& output = measured[segment.start + local];
      output.available = true;
      output.unit.projection_offset = segment.start + local;
      output.unit.segment_id = segment.segment_id;
      output.unit.owner_id = segment.owner_id;
      output.unit.local_start = local;
      output.unit.local_end = local + 1;
      output.unit.bounds = ToEditRect(bounds);
      if (segment.block) {
        output.unit.flags = editing::EditingLayoutUnitFlag::kBlock;
      }
    }
  }
  return measured;
}

void TextEditContextSessionClay::RefreshGeometry(
    editing::TextRange requested, uint64_t state_revision,
    uint64_t projection_revision) {
  if (!session_ || !host_ || !active_) {
    return;
  }
  const auto state = session_->Snapshot();
  const auto projection = session_->ProjectionSnapshot();
  if (state.revision != state_revision ||
      projection.revision != projection_revision) {
    return;
  }
  const auto measured = BuildLayoutUnits(projection);
  size_t seed = std::min(requested.start(), projection.length);
  if (seed == projection.length && seed > 0) {
    --seed;
  }
  if (projection.length > 0 && !measured[seed].available) {
    for (size_t i = requested.start(); i < requested.end() &&
                                         i < measured.size();
         ++i) {
      if (measured[i].available) {
        seed = i;
        break;
      }
    }
  }
  size_t coverage_start = seed;
  size_t coverage_end = seed;
  if (projection.length > 0 && measured[seed].available) {
    while (coverage_start > 0 && measured[coverage_start - 1].available) {
      --coverage_start;
    }
    coverage_end = seed + 1;
    while (coverage_end < measured.size() && measured[coverage_end].available) {
      ++coverage_end;
    }
  }

  editing::EditingGeometrySnapshot geometry;
  geometry.state_revision = state.revision;
  geometry.projection_revision = projection.revision;
  geometry.projection_length = projection.length;
  geometry.coverage = editing::TextRange(coverage_start, coverage_end);
  geometry.control_bounds = ToEditRect(host_->BoundsRelativeTo(nullptr));
  for (size_t offset = coverage_start; offset < coverage_end; ++offset) {
    geometry.units.push_back(measured[offset].unit);
  }
  if (session_->UpdateGeometry(std::move(geometry)) !=
      editing::EditingOperationStatus::kAccepted) {
    return;
  }
  const auto rects = session_->SelectionRects(state.selection, state.revision);
  if (!rects.empty() && text_input_controller_) {
    const auto& caret =
        state.selection.reversed() ? rects.front() : rects.back();
    text_input_controller_->SetCaretRect(
        clay::FloatRect(caret.x, caret.y, caret.width, caret.height));
  }
}

void TextEditContextSessionClay::RefreshSelectionCallbacks(
    const editing::EditingProjectionSnapshot& projection) {
  if (!host_) {
    return;
  }
  std::unordered_set<int64_t> next;
  for (const auto& segment : projection.segments) {
    if (segment.kind == editing::EditingSegmentKind::kText &&
        segment.owner_id >= 0) {
      next.insert(segment.owner_id);
    }
  }
  clay::ViewContext* context = host_->page_view()->GetViewContext();
  for (int64_t owner_id : callback_owner_ids_) {
    if (next.count(owner_id) == 0) {
      if (auto* view = FindTextView(context, owner_id)) {
        view->SetEditContextSelectionChangedCallback(nullptr);
        view->SetEditContextHitTestCallback(nullptr);
      }
    }
  }
  auto weak = weak_factory_.GetWeakPtr();
  for (int64_t owner_id : next) {
    if (auto* view = FindTextView(context, owner_id)) {
      view->SetEditContextSelectionChangedCallback(
          [weak, owner_id](int start, int end) {
            if (weak) {
              weak->HandleViewSelectionChanged(owner_id, start, end);
            }
          });
      view->SetEditContextHitTestCallback(
          [weak](const clay::FloatPoint& point, bool extend) {
            if (weak) {
              weak->HandlePointerSelection(point, extend);
            }
          });
    }
  }
  callback_owner_ids_ = std::move(next);
}

void TextEditContextSessionClay::UpdateRenderedSelection(
    const editing::EditingStateSnapshot& snapshot,
    const editing::EditingProjectionSnapshot& projection) {
  if (!host_) {
    return;
  }
  applying_snapshot_ = true;
  clay::ViewContext* context = host_->page_view()->GetViewContext();
  for (const auto& segment : projection.segments) {
    if (segment.kind != editing::EditingSegmentKind::kText) {
      continue;
    }
    auto* view = FindTextView(context, segment.owner_id);
    if (!view) {
      continue;
    }
    const size_t local_base = std::clamp(snapshot.selection.base(),
                                         segment.start, segment.end) -
                              segment.start;
    const size_t local_extent = std::clamp(snapshot.selection.extent(),
                                           segment.start, segment.end) -
                                segment.start;
    view->GetRenderText()->SetSelection(
        clay::TextRange(local_base, local_extent));
  }
  applying_snapshot_ = false;
}

std::optional<size_t> TextEditContextSessionClay::MapViewOffsetToProjection(
    int64_t owner_id, size_t offset) const {
  for (const auto& segment : projection_.segments) {
    if (segment.kind == editing::EditingSegmentKind::kText &&
        segment.owner_id == owner_id && offset <= segment.end - segment.start) {
      return segment.start + offset;
    }
  }
  return std::nullopt;
}

void TextEditContextSessionClay::HandleViewSelectionChanged(
    int64_t owner_id, int start, int end) {
  if (!session_ || applying_snapshot_ || start < 0 || end < 0) {
    return;
  }
  auto base = MapViewOffsetToProjection(owner_id, static_cast<size_t>(start));
  auto extent = MapViewOffsetToProjection(owner_id, static_cast<size_t>(end));
  if (!base || !extent) {
    return;
  }
  const auto result = session_->SetSelection(
      editing::TextRange(*base, *extent), snapshot_.revision);
  ApplySnapshot(result.snapshot, result.restart_input || !result.accepted(),
                result.accepted());
}

void TextEditContextSessionClay::HandlePointerSelection(
    const clay::FloatPoint& point, bool extend) {
  if (!session_ || !active_) {
    return;
  }
  const auto state = session_->Snapshot();
  const auto projection = session_->ProjectionSnapshot();
  RefreshGeometry(editing::TextRange(0, projection.length), state.revision,
                  projection.revision);
  if (!extend) {
    pointer_anchor_.reset();
  }
  auto result = session_->SetSelectionFromPoint(
      {point.x(), point.y()}, extend ? pointer_anchor_ : std::nullopt,
      state.revision);
  if (result.accepted() && !extend) {
    pointer_anchor_ = result.snapshot.selection.extent();
  }
  ApplySnapshot(result.snapshot, result.restart_input || !result.accepted(),
                result.accepted());
}

}  // namespace lynx::tasm
