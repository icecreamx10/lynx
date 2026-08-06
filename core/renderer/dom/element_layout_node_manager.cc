// Copyright 2025 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include "core/renderer/dom/element_layout_node_manager.h"

#include <memory>
#include <optional>
#include <sstream>
#include <utility>

#include "base/include/string/string_utils.h"
#include "core/renderer/css/css_property.h"
#include "core/renderer/dom/fiber/raw_text_element.h"
#include "core/renderer/dom/fiber/text_element.h"
#include "core/renderer/dom/element_manager.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "core/renderer/starlight/style/auto_gen_css_type.h"
#include "core/renderer/starlight/types/layout_constraints.h"

namespace lynx {
namespace tasm {

namespace {

std::u16string ToUtf16(const base::String& value) {
  return base::U8StringToU16(value.str());
}

std::u16string AttributeText(const lepus::Value& value) {
  base::String result = value.String();
  if (result.empty()) {
    if (value.IsInt32()) {
      result = base::String(std::to_string(value.Int32()));
    } else if (value.IsInt64()) {
      result = base::String(std::to_string(value.Int64()));
    } else if (value.IsNumber()) {
      std::stringstream stream;
      stream << value.Number();
      result = base::String(stream.str());
    } else if (value.IsNaN()) {
      result = base::String("NaN");
    } else if (value.IsNil()) {
      result = base::String("null");
    } else if (value.IsUndefined()) {
      result = base::String("undefined");
    }
  }
  return ToUtf16(result);
}

std::u16string ElementText(Element* element) {
  if (!element) {
    return {};
  }
  if (element->is_raw_text()) {
    const auto& content = static_cast<RawTextElement*>(element)->content();
    if (!content.empty()) {
      return ToUtf16(content);
    }
  } else if (element->is_text()) {
    const auto& content = static_cast<TextElement*>(element)->content();
    if (!content.empty()) {
      return ToUtf16(content);
    }
  }

  const auto& attributes = element->GetAttributesForWorklet();
  BASE_STATIC_STRING_DECL(kTextAttribute, "text");
  const auto text = attributes.find(kTextAttribute);
  return text == attributes.end() ? std::u16string{}
                                  : AttributeText(text->second);
}

bool IsExcludedAtomicSubtree(Element* element) {
  const auto& attributes = element->GetAttributesForWorklet();
  BASE_STATIC_STRING_DECL(kContentEditableAttribute, "contenteditable");
  const auto content_editable = attributes.find(kContentEditableAttribute);
  return content_editable != attributes.end() &&
         content_editable->second.IsString() &&
         content_editable->second.String().IsEqual("false");
}

std::u16string CollectPlainTextFallback(Element* element) {
  if (!element) {
    return {};
  }
  if (element->is_raw_text()) {
    return ElementText(element);
  }

  std::u16string result;
  const auto& attributes = element->GetAttributesForWorklet();
  BASE_STATIC_STRING_DECL(kTextAttribute, "text");
  const auto text = attributes.find(kTextAttribute);
  if (text != attributes.end()) {
    result += AttributeText(text->second);
  } else {
    result += ElementText(element);
  }
  for (const auto& child : element->children()) {
    result += CollectPlainTextFallback(child.get());
  }
  return result;
}

bool IsBlock(Element* element) {
  const auto display = element->GetElementStyle(kPropertyIDDisplay);
  if (display && display->IsEnum() &&
      display->GetEnum<starlight::DisplayType>() ==
          starlight::DisplayType::kBlock) {
    return true;
  }

  const auto& inline_styles = element->GetCurrentRawInlineStyles();
  if (!inline_styles) {
    return false;
  }
  const auto raw_display = inline_styles->find(kPropertyIDDisplay);
  return raw_display != inline_styles->end() &&
         raw_display->second.IsString() &&
         raw_display->second.String().IsEqual("block");
}

void AppendElement(Element* element, editing::EditingProjection* projection,
                   std::optional<int64_t> inherited_text_owner) {
  if (!element || !projection) {
    return;
  }

  const bool block = IsBlock(element);
  if (IsExcludedAtomicSubtree(element) || element->is_image()) {
    projection->AppendAtomicObject(element->impl_id(),
                                   CollectPlainTextFallback(element), block);
    return;
  }

  if (block) {
    projection->AppendBlockBoundary(
        element->impl_id(), editing::EditingBlockBoundaryEdge::kLeading);
  }

  if (element->is_raw_text()) {
    const int64_t owner_id =
        inherited_text_owner.value_or(element->impl_id());
    projection->AppendText(owner_id, ElementText(element), element->impl_id());
  } else if (element->is_text()) {
    const int64_t owner_id =
        inherited_text_owner.value_or(element->impl_id());
    projection->AppendText(owner_id, ElementText(element), element->impl_id());
  }

  const std::optional<int64_t> text_owner =
      element->is_text()
          ? std::optional<int64_t>(
                inherited_text_owner.value_or(element->impl_id()))
          : std::nullopt;
  for (const auto& child : element->children()) {
    const bool shares_text_owner =
        text_owner && (child->is_text() || child->is_raw_text());
    AppendElement(child.get(), projection,
                  shares_text_owner ? text_owner : std::nullopt);
  }

  if (block) {
    projection->AppendBlockBoundary(
        element->impl_id(), editing::EditingBlockBoundaryEdge::kTrailing);
  }
}

}  // namespace

editing::EditingProjection BuildEditingProjection(Element* host) {
  editing::EditingProjection projection;
  if (!host) {
    return projection;
  }
  if (host->children().empty() && host->is_text()) {
    projection.AppendText(host->impl_id(), ElementText(host), host->impl_id());
    return projection;
  }

  const std::optional<int64_t> host_owner =
      host->is_text() ? std::optional<int64_t>(host->impl_id()) : std::nullopt;
  for (const auto& child : host->children()) {
    const bool shares_host_owner =
        host_owner && (child->is_text() || child->is_raw_text());
    AppendElement(child.get(), &projection,
                  shares_host_owner ? host_owner : std::nullopt);
  }
  return projection;
}

ElementLayoutNodeManager::ElementLayoutNodeManager(
    ElementManager& element_manager)
    : element_manager_(element_manager) {}

void ElementLayoutNodeManager::SetMeasureFunc(
    int32_t id, std::unique_ptr<MeasureFunc> measure_func) {
  auto* element = GetElement(id);
  if (element && element->IsShadowNodeCustom()) {
    element->SetMeasureFunc(std::move(measure_func));
  }
}

void ElementLayoutNodeManager::MarkDirtyAndRequestLayout(int32_t id) {
  auto* element = GetElement(id);
  if (element) {
    element->MarkLayoutDirty();
  }
}

void ElementLayoutNodeManager::MarkDirtyAndForceLayout(int32_t id) {
  auto* element = GetElement(id);
  if (element) {
    element->MarkLayoutDirty();
  }
}

bool ElementLayoutNodeManager::IsDirty(int32_t id) { return false; }

FlexDirection ElementLayoutNodeManager::GetFlexDirection(int32_t id) {
  return FlexDirection::kRow;
}

float ElementLayoutNodeManager::GetWidth(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetHeight(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetMinWidth(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetMaxWidth(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetMinHeight(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetMaxHeight(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetPaddingLeft(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetPaddingTop(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetPaddingRight(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetPaddingBottom(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetMarginLeft(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetMarginTop(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetMarginRight(int32_t id) { return 0; }

float ElementLayoutNodeManager::GetMarginBottom(int32_t id) { return 0; }

LayoutResult ElementLayoutNodeManager::UpdateMeasureByPlatform(
    int32_t id, float width, int32_t width_mode, float height,
    int32_t height_mode, bool final_measure) {
  auto* element = GetElement(id);
  if (element == nullptr || element->slnode() == nullptr) {
    return LayoutResult();
  }

  starlight::Constraints constraints;
  constraints[starlight::kHorizontal] = starlight::OneSideConstraint(
      width, static_cast<SLMeasureMode>(width_mode));
  constraints[starlight::kVertical] = starlight::OneSideConstraint(
      height, static_cast<SLMeasureMode>(height_mode));
  FloatSize result =
      element->slnode()->UpdateMeasureByPlatform(constraints, final_measure);
  return LayoutResult(result.width_, result.height_, result.baseline_);
}

void ElementLayoutNodeManager::AlignmentByPlatform(int32_t id, float offset_top,
                                                   float offset_left) {
  auto* element = GetElement(id);
  if (element == nullptr || element->slnode() == nullptr) {
    return;
  }
  element->slnode()->AlignmentByPlatform(offset_top, offset_left);
}

void ElementLayoutNodeManager::DestroyLayoutNode(int32_t id) {
  destroyed_layout_node_ids_.insert(id);
}

void ElementLayoutNodeManager::DestroyPlatformLayoutNodes() {
  if (!destroyed_layout_node_ids_.empty()) {
    element_manager_.layout_context()->DestroyLayoutNodes(
        destroyed_layout_node_ids_);
    destroyed_layout_node_ids_.clear();
  }
}

Element* ElementLayoutNodeManager::GetElement(int32_t id) const {
  return element_manager_.node_manager()->Get(id);
}

}  // namespace tasm
}  // namespace lynx
