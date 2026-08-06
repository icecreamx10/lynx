// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "clay/ui/rendering/text/render_text.h"

#include <algorithm>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "clay/gfx/geometry/float_point.h"
#include "clay/gfx/geometry/float_rect.h"
#include "clay/gfx/rendering_backend.h"
#include "clay/gfx/style/color_source.h"
#include "clay/gfx/style/tile_mode.h"
#include "clay/ui/common/text_input_type_traits.h"
#include "clay/ui/component/editable/text_utils.h"
#include "clay/ui/component/text/inline_emoji_bitmap.h"
#include "clay/ui/component/text/text_style.h"
#include "clay/ui/painter/gradient_factory.h"
#include "clay/ui/painter/text_painter.h"
#include "clay/ui/rendering/renderer.h"

namespace clay {

namespace {

constexpr uint32_t kSelectionColor = 0x402196F3;  // material blue[200]
#if defined(OS_MAC) || defined(OS_WIN)
constexpr float kCaretWidth = 1.f;
#else
constexpr float kCaretWidth = 2.f;
#endif
constexpr float kCaretVerticalPreserveSpace = 2.f;

}  // namespace

RenderText::RenderText() : painter_(std::make_unique<TextPainter>()) {}

RenderText::~RenderText() { paragraph_ = nullptr; }

const char* RenderText::GetName() const { return "RenderText"; }

void RenderText::SetParagraph(txt::Paragraph* paragraph,
                              const std::u16string& text) {
  paragraph_ = paragraph;
  text_ = text;
  painter_->SetParagraph(paragraph_);
  MarkNeedsPaint();
}

void RenderText::SetGradient(const std::optional<Gradient>& gradient) {
  painter_->SetGradient(gradient);
  MarkNeedsPaint();
}

void RenderText::SetGradientShaderMap(
    std::map<int, std::shared_ptr<ColorSource>>&& gradient_shader_map,
    std::map<int, std::pair<size_t, size_t>>&& range_map) {
  painter_->SetGradientShaderMap(std::move(gradient_shader_map),
                                 std::move(range_map));
  MarkNeedsPaint();
}

void RenderText::SetTextStrokeMap(
    std::unordered_map<int, TextStroke>&& text_stroke_map) {
  painter_->SetTextStrokeMap(std::move(text_stroke_map));
  MarkNeedsPaint();
}

void RenderText::SetInlineEmojiInfo(
    std::vector<InlineEmojiInfo> inline_emoji_info) {
  inline_emojis_.clear();
  for (auto& info : inline_emoji_info) {
    auto image = CreateInlineEmojiGraphicsImage(info.bitmap);
    if (info.placeholder_id >= 0 && image) {
      inline_emojis_.emplace(info.placeholder_id,
                             InlineEmojiRenderInfo{std::move(image)});
    }
  }
  MarkNeedsPaint();
}

bool RenderText::IsInlineEmojiPlaceholder(int placeholder_id) const {
  return inline_emojis_.find(placeholder_id) != inline_emojis_.end();
}

void RenderText::Paint(PaintingContext& context, const FloatPoint& offset) {
  if (HasBackgroundClipText() && painter_->CanPaint()) {
    auto painter = [this](PaintingContext& ctx,
                          const FloatPoint& layer_offset) {
      RenderBox::Paint(ctx, layer_offset);
      // Paint inline children background first.
      RenderBox::PaintChildren(ctx, layer_offset);
    };

    GraphicsContext child_context(
        context.GetGraphicsContext()->GetUnrefQueue());
    skity::Rect rect = skity::Rect::MakeXYWH(0, 0, Width(), Height());
    child_context.BeginRecording(rect);
    FloatPoint paint_offset = offset + PaintOffset();
    // Draw text into a mask that can be used to clip background.
    PaintText(&child_context, paint_offset);
    auto picture = child_context.FinishRecording();
    // TODO(Jinsong): Generate a Lazy PaintImage to avoid blocking.
    auto image = renderer_->renderer_client()->MakeRasterSnapshot(
        picture->picture()->raw(),
        skity::Vec2(ContentWidth(), ContentHeight()));

    if (image) {
      auto gradient_shader = std::make_shared<ImageColorSource>(
          image, TileMode::kDecal, TileMode::kDecal);
      context.PushShaderMask(gradient_shader,
                             FloatRect(paint_offset.x(), paint_offset.y(),
                                       ClientWidth(), ClientHeight()),
                             BlendMode::kDstIn, offset, painter);
      return;
    }
  }
  // Draw text as normal.
  RenderBox::Paint(context, offset);
  // Paint inline children background first.
  RenderBox::PaintChildren(context, offset);
  if (painter_->CanPaint()) {
    FloatPoint paint_offset = offset + PaintOffset();
    GraphicsContext* graphics_context = context.GetGraphicsContext();
    PaintText(graphics_context, paint_offset);
  }
}

void RenderText::PaintText(GraphicsContext* graphics_context,
                           const FloatPoint& offset) {
  GraphicsContext::AutoRestore saver(graphics_context, true);
  graphics_context->Translate(offset.x(), offset.y());
  bool needs_clip_x = Overflow() == CSSProperty::OVERFLOW_Y ||
                      Overflow() == CSSProperty::OVERFLOW_HIDDEN;
  bool needs_clip_y = Overflow() == CSSProperty::OVERFLOW_X ||
                      Overflow() == CSSProperty::OVERFLOW_HIDDEN;
  if (!needs_clip_x && needs_clip_y) {
    skity::Rect rect = skity::Rect::MakeXYWH(
        -renderer_->GetFrameSize().width(), 0,
        2 * renderer_->GetFrameSize().width(), ContentHeight());
    graphics_context->ClipRect(rect, GrClipOp::kIntersect, false);
  } else if (needs_clip_x && !needs_clip_y) {
    skity::Rect rect = skity::Rect::MakeXYWH(
        0, -renderer_->GetFrameSize().height(), ContentWidth(),
        2 * renderer_->GetFrameSize().height());
    graphics_context->ClipRect(rect, GrClipOp::kIntersect, false);
  } else if (needs_clip_x && needs_clip_y) {
    skity::Rect rect =
        skity::Rect::MakeXYWH(0, 0, ContentWidth(), ContentHeight());
    graphics_context->ClipRect(rect, GrClipOp::kIntersect, false);
  }
  painter_->SetWidth(ContentWidth());
  painter_->SetHeight(ContentHeight());
  auto paragraph_content_offset = GetParagraphPaintOffset() - PaintOffset();
  if (HasColorRasterAnimation()) {
    graphics_context->Canvas()->OnDrawDynamicTextBlobsStart();
    painter_->Paint(graphics_context, paragraph_content_offset.x(),
                    paragraph_content_offset.y());
    PaintInlineEmojis(graphics_context, paragraph_content_offset.x(),
                      paragraph_content_offset.y());
    graphics_context->Canvas()->OnDrawDynamicTextBlobsEnd();
  } else {
    painter_->Paint(graphics_context, paragraph_content_offset.x(),
                    paragraph_content_offset.y());
    PaintInlineEmojis(graphics_context, paragraph_content_offset.x(),
                      paragraph_content_offset.y());
  }
  if (select_end_ != select_start_) {
    PaintSelection(graphics_context);
  } else if (display_caret_ && select_end_ >= 0) {
    PaintCaret(graphics_context, paragraph_content_offset);
  }
}

void RenderText::SetCaretDisplay(bool display) {
  if (display_caret_ != display) {
    display_caret_ = display;
    MarkNeedsPaint();
  }
}

void RenderText::SetCaretColor(std::optional<Color> color) {
  if (caret_color_ != color) {
    caret_color_ = color;
    MarkNeedsPaint();
  }
}

void RenderText::SetCaretFallbackColor(const Color& color) {
  if (caret_fallback_color_ != color) {
    caret_fallback_color_ = color;
    if (!caret_color_) {
      MarkNeedsPaint();
    }
  }
}

void RenderText::SetCaretGradient(std::optional<Gradient> gradient) {
  if (caret_gradient_ != gradient) {
    caret_gradient_ = std::move(gradient);
    MarkNeedsPaint();
  }
}

void RenderText::SetCaretWidth(float width) {
  std::optional<float> next_width =
      width > 0.f ? std::make_optional(width) : std::nullopt;
  if (caret_width_ != next_width) {
    caret_width_ = std::move(next_width);
    MarkNeedsPaint();
  }
}

void RenderText::SetCaretHeight(float height) {
  std::optional<float> next_height =
      height > 0.f ? std::make_optional(height) : std::nullopt;
  if (caret_height_ != next_height) {
    caret_height_ = std::move(next_height);
    MarkNeedsPaint();
  }
}

void RenderText::SetCaretRadius(float radius) {
  std::optional<float> next_radius =
      radius > 0.f ? std::make_optional(radius) : std::nullopt;
  if (caret_radius_ != next_radius) {
    caret_radius_ = std::move(next_radius);
    MarkNeedsPaint();
  }
}

float RenderText::CaretWidth() const {
  if (caret_width_ && *caret_width_ > 0.f) {
    return *caret_width_;
  }
  return renderer_ ? renderer_->ConvertFrom<kPixelTypeLogical>(kCaretWidth)
                   : kCaretWidth;
}

FloatRect RenderText::ComputeCaretRect() const {
  const float preserve_space = renderer_
                                   ? renderer_->ConvertFrom<kPixelTypeLogical>(
                                         kCaretVerticalPreserveSpace)
                                   : kCaretVerticalPreserveSpace;
  const float paragraph_height =
      paragraph_ ? static_cast<float>(paragraph_->GetHeight()) : 0.f;
  FloatRect caret(0.f, preserve_space, CaretWidth(),
                  std::max(0.f, paragraph_height - 2.f * preserve_space));
  if (!painter_ || !paragraph_ || text_.empty() || select_end_ < 0) {
    return caret;
  }

  const int length = static_cast<int>(text_.length());
  const int caret_offset = std::clamp(select_end_, 0, length);
  auto update_from_box = [&caret](const TextBox& box, bool after) {
    caret.SetX(after ? box.rect.MaxX() : box.rect.x());
    caret.SetY(box.rect.y());
    caret.SetHeight(box.rect.height());
  };
  auto downstream = [&]() {
    if (caret_offset >= length) {
      return false;
    }
    const size_t next_code_unit = text_.at(caret_offset);
    const bool needs_search =
        TextUtils::IsHighSurrogate(next_code_unit) ||
        TextUtils::IsLowSurrogate(next_code_unit) ||
        next_code_unit == TextUtils::kZWJUtf16 ||
        TextUtils::IsUnicodeDirectionality(next_code_unit);
    int cluster_length = needs_search ? 2 : 1;
    std::vector<TextBox> boxes;
    while (boxes.empty()) {
      const int next_offset = std::min(length, caret_offset + cluster_length);
      boxes = painter_->GetRectsForRange(caret_offset, next_offset,
                                         RectHeightStyle::kStrut);
      if (!boxes.empty() || !needs_search || next_offset >= length) {
        break;
      }
      cluster_length *= 2;
    }
    if (boxes.empty() || boxes.front().rect.height() <= 0.f) {
      return false;
    }
    update_from_box(boxes.front(), false);
    return true;
  };
  auto upstream = [&]() {
    if (caret_offset <= 0) {
      return false;
    }
    const size_t previous_code_unit = text_.at(caret_offset - 1);
    const bool needs_search =
        TextUtils::IsHighSurrogate(previous_code_unit) ||
        TextUtils::IsLowSurrogate(previous_code_unit) ||
        previous_code_unit == TextUtils::kZWJUtf16 ||
        TextUtils::IsUnicodeDirectionality(previous_code_unit);
    int cluster_length = needs_search ? 2 : 1;
    std::vector<TextBox> boxes;
    while (boxes.empty()) {
      const int previous_offset = std::max(0, caret_offset - cluster_length);
      boxes = painter_->GetRectsForRange(previous_offset, caret_offset,
                                         RectHeightStyle::kMax);
      if (!boxes.empty() || !needs_search || previous_offset == 0) {
        break;
      }
      cluster_length *= 2;
    }
    if (boxes.empty() || boxes.back().rect.height() <= 0.f) {
      return false;
    }
    update_from_box(boxes.back(), true);
    return true;
  };

  if (!downstream()) {
    upstream();
  }
  return caret;
}

void RenderText::PaintCaret(GraphicsContext* graphics_context,
                            const FloatPoint& paragraph_offset) {
  FloatRect paint_rect = ComputeCaretRect();
  paint_rect.Move(paragraph_offset.x(), paragraph_offset.y());
  if (caret_height_ && *caret_height_ > 0.f) {
    const float height = *caret_height_;
    paint_rect.SetY(paint_rect.y() + (paint_rect.height() - height) * 0.5f);
    paint_rect.SetHeight(height);
  }
  if (paint_rect.width() <= 0.f || paint_rect.height() <= 0.f) {
    return;
  }

  class Paint paint;
  const auto fallback_color =
      caret_color_.value_or(caret_fallback_color_).Value();
  if (caret_gradient_) {
    auto shader = GradientFactory::CreateShader(*caret_gradient_, paint_rect);
    shader ? paint.setColorSource(shader) : paint.setColor(fallback_color);
  } else {
    paint.setColor(fallback_color);
  }

  if (caret_radius_ && *caret_radius_ > 0.f) {
    const float radius =
        std::min(*caret_radius_,
                 std::min(paint_rect.width(), paint_rect.height()) * 0.5f);
    paint.setAntiAlias(true);
    graphics_context->DrawRRect(
        skity::RRect::MakeRectXY(paint_rect, radius, radius), paint);
  } else {
    graphics_context->DrawRect(paint_rect, paint);
  }
}

FloatPoint RenderText::GetParagraphPaintOffset() const {
  double x_offset = 0;
  // Not aligned with web behavior in bidirectional text but consistent
  // with lynx behavior.
  if (paragraph_ && text_paint_align_ == TextAlignment::kCenter) {
    x_offset =
        std::max(0.0, (ContentWidth() - paragraph_->GetLongestLine()) / 2);
  } else if (paragraph_ && text_paint_align_ == TextAlignment::kRight) {
    x_offset = std::max(0.0, ContentWidth() - paragraph_->GetLongestLine());
  } else {
    FML_DCHECK(text_paint_align_ == TextAlignment::kLeft);
  }
  return PaintOffset() + FloatPoint(static_cast<float>(x_offset),
                                    static_cast<float>(line_spacing_offset_));
}

void RenderText::PaintInlineEmojis(GraphicsContext* graphics_context,
                                   double x_offset, double y_offset) {
  if (!paragraph_ || inline_emojis_.empty()) {
    return;
  }
  class Paint paint;
  paint.setAntiAlias(false);
  for (const auto& box : paragraph_->GetRectsForPlaceholders()) {
    auto it = inline_emojis_.find(static_cast<int>(box.placeholder_id));
    if (it == inline_emojis_.end() || !it->second.image) {
      continue;
    }
    auto dst = box.rect;
    dst.Offset(x_offset, y_offset);
    graphics_context->DrawImageRect(it->second.image, dst,
                                    SAMPLING_OPTIONS(FilterMode::kNearest, 0),
                                    &paint);
  }
}

void RenderText::SetSelection(const TextRange& range) {
  select_start_ = range.start();
  select_end_ = range.end();
  if (selection_changed_callback_) {
    selection_changed_callback_(select_start_, select_end_);
  }
  pre_select_end_ = select_end_;
  MarkNeedsPaint();
}

void RenderText::SetAllSelection() {
  SetSelection(TextRange(0, text_.length()));
}

void RenderText::PaintSelection(GraphicsContext* context) {
  class Paint paint;
  paint.setColor(kSelectionColor);
  auto text_boxes =
      painter_->GetRectsForRange(std::min(select_start_, select_end_),
                                 std::max(select_start_, select_end_),
                                 RectHeightStyle::kMax, RectWidthStyle::kMax);
  clay::GrPath path;
  for (auto box : text_boxes) {
    PATH_ADD_RECT(path, box.rect);
  }
  context->DrawPath(path, paint);
}

std::u16string RenderText::GetSelectionString() const {
  return text_.substr(std::min(select_start_, select_end_),
                      std::abs(select_end_ - select_start_));
}

std::vector<Point> RenderText::GetPointsFromRangeSelection(
    int select_start, int select_end) const {
  if (select_start == select_end) {
    return std::vector<Point>{};
  } else {
    std::vector<TextBox> boxes = painter_->GetRectsForRange(
        std::min(select_start, select_end), std::max(select_start, select_end));
    if (boxes.empty()) {
      return std::vector<Point>();
    }
    // TODO(wangyanyi) now it is assumed textdirection is 'left'
    return std::vector<Point>{
        Point(boxes.front().GetLeft(), boxes.front().GetBottom()),
        Point(boxes.back().GetRight(), boxes.back().GetBottom())};
  }
}

TextBox RenderText::GetEndTextPositionTopAndBottom() const {
  std::vector<TextBox> boxes;
  if (select_start_ <= select_end_) {
    boxes = painter_->GetRectsForRange(select_end_ - 1, select_end_);
  } else {
    boxes = painter_->GetRectsForRange(select_end_, select_end_ + 1);
  }

  if (boxes.empty()) {
    return TextBox(FloatRect());
  }
  return boxes.front();
}

TextBox RenderText::GetStartTextPositionTopAndBottom() const {
  std::vector<TextBox> boxes;
  if (select_start_ <= select_end_) {
    boxes = painter_->GetRectsForRange(select_start_, select_start_ + 1);
  } else {
    boxes = painter_->GetRectsForRange(select_start_ - 1, select_start_);
  }
  if (boxes.empty()) {
    TextBox(FloatRect());
  }
  return boxes.front();
}

TextBox RenderText::GetLeftTextBox() {
  std::vector<TextBox> boxes;
  boxes = painter_->GetRectsForRange(std::min(select_start_, select_end_),
                                     std::min(select_start_, select_end_) + 1);
  if (boxes.empty()) {
    TextBox(FloatRect());
  }
  return boxes.front();
}

TextBox RenderText::GetRightTextBox() {
  std::vector<TextBox> boxes;
  boxes = painter_->GetRectsForRange(std::max(select_start_, select_end_) - 1,
                                     std::max(select_start_, select_end_));
  if (boxes.empty()) {
    TextBox(FloatRect());
  }
  return boxes.back();
}

std::vector<FloatRect> RenderText::GetTextLineRects(int start, int end) {
  return painter_->GetTextLineRects(start, end);
}

FloatRect RenderText::GetTextBoundingRect(
    int start, int end, const std::vector<FloatRect>& line_rect) {
  if (line_rect.empty()) {
    return FloatRect();
  }

  FloatRect result = line_rect.front();

  for (auto rect : line_rect) {
    result.ExpandToInclude(rect);
  }
  return result;
}

}  // namespace clay
