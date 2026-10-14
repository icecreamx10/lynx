// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include "platform/wasm/browser_text.h"

#include <emscripten.h>
#include <textra/fontmgr_collection.h>
#include <textra/layout_drawer.h>
#include <textra/paragraph.h>
#include <textra/platform/skity/skity_canvas_helper.h>
#include <textra/platform/skity/skity_typeface_helper.h>
#include <textra/text_layout.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <skity/io/data.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/renderer/dom/fiber/raw_text_element.h"
#include "core/renderer/dom/fiber/text_element.h"
#include "core/renderer/starlight/layout/layout_object.h"

EM_JS(void, RequestBrowserFont, (int slot),
      { Module.lynxFonts.request(slot); });

namespace lynx::wasm {
namespace {
namespace tt = ttoffice::tttext;
namespace sl = starlight;
using Face = std::shared_ptr<skity::textlayout::SkityTypefaceHelper>;
// A candidate page may measure before validation. Only the accepted page is
// allowed to start asynchronous font work against its resource generation.
bool font_requests_enabled = false;
struct FontRequestScope {
  bool previous;
  explicit FontRequestScope(bool enabled) : previous(font_requests_enabled) {
    font_requests_enabled = enabled;
  }
  ~FontRequestScope() { font_requests_enabled = previous; }
};
class FontManager final : public tt::IFontManager {
 public:
  bool Register(int slot, const uint8_t* bytes, size_t size) {
    if (slot < 0 || slot >= 4 || !bytes || !size || size > 32 * 1024 * 1024)
      return false;
    if (fonts_[slot]) return true;
    auto face =
        skity::Typeface::MakeFromData(skity::Data::MakeWithCopy(bytes, size));
    if (!face) return false;
    fonts_[slot] = std::move(face);
    instances_.clear();
    return true;
  }
  int countFamilies() const override {
    return std::count_if(fonts_.begin(), fonts_.end(),
                         [](const auto& font) { return bool(font); });
  }
  void EnsureFamily(const char* name) const {
    auto slot = Family(name);
    if (font_requests_enabled && !fonts_[slot]) RequestBrowserFont(slot);
  }
  void Ensure(uint32_t cp) const {
    int slot = -1;
    if ((cp >= 0x2e80 && cp <= 0xa4cf) || (cp >= 0xf900 && cp <= 0xfaff) ||
        (cp >= 0xff00 && cp <= 0xffef) || (cp >= 0x20000 && cp <= 0x323af))
      slot = 1;
    else if ((cp >= 0x600 && cp <= 0x8ff) || (cp >= 0xfb50 && cp <= 0xfdff) ||
             (cp >= 0xfe70 && cp <= 0xfeff))
      slot = 2;
    else if ((cp >= 0x1f000 && cp <= 0x1faff) || (cp >= 0x2600 && cp <= 0x27bf))
      slot = 3;
    if (font_requests_enabled && slot >= 0 && !fonts_[slot])
      RequestBrowserFont(slot);
  }
  bool Supports(uint32_t cp) const {
    if (cp < 0x20 || cp == 0x7f || (cp >= 0x200b && cp <= 0x200f) ||
        (cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2060 && cp <= 0x206f) ||
        (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xe0000 && cp <= 0xe007f) ||
        (cp >= 0xe0100 && cp <= 0xe01ef))
      return true;
    for (const auto& face : fonts_)
      if (face && face->UnicharToGlyph(cp)) return true;
    return false;
  }
  TypefaceRef matchFamilyStyle(const char* family,
                               const tt::FontStyle& style) override {
    return Get(Family(family), style.GetWeight());
  }
  TypefaceRef matchFamilyStyleCharacter(const char* family,
                                        const tt::FontStyle& style,
                                        const char**, int,
                                        uint32_t cp) override {
    auto first = Get(Family(family), style.GetWeight());
    if (first && first->UnicharToGlyph(cp)) return first;
    for (size_t i = 0; i < fonts_.size(); ++i) {
      if (!fonts_[i]) continue;
      auto face = Get(i, style.GetWeight());
      if (face && face->UnicharToGlyph(cp)) return face;
    }
    return nullptr;
  }
  TypefaceRef makeFromFile(const char* path, int) override {
    auto face = skity::Typeface::MakeFromFile(path);
    return face ? std::make_shared<skity::textlayout::SkityTypefaceHelper>(face)
                : nullptr;
  }
  TypefaceRef legacyMakeTypeface(const char* family,
                                 tt::FontStyle style) const override {
    return Get(Family(family), style.GetWeight());
  }

 private:
  size_t Family(const char* name) const {
    std::string family = name ? name : "";
    if (family == "Noto Sans SC" || family == "NotoSansSC") return 1;
    if (family == "Noto Sans Arabic") return 2;
    if (family == "Noto Emoji") return 3;
    return 0;
  }
  Face Get(size_t index, int weight) const {
    if (!fonts_[index]) {
      if (font_requests_enabled) RequestBrowserFont(index);
      index = 0;
    }
    if (!fonts_[index]) return nullptr;
    auto key = index * 1001 + std::clamp(weight, 100, 900);
    auto found = instances_.find(key);
    if (found != instances_.end()) return found->second;
    skity::VariationPosition position;
    position.AddCoordinate(skity::SetFourByteTag('w', 'g', 'h', 't'), weight);
    skity::FontArguments args;
    args.SetVariationDesignPosition(position);
    auto face = fonts_[index]->MakeVariation(args);
    auto wrapped = std::make_shared<skity::textlayout::SkityTypefaceHelper>(
        face ? face : fonts_[index]);
    instances_[key] = wrapped;
    return wrapped;
  }
  std::array<std::shared_ptr<skity::Typeface>, 4> fonts_;
  mutable std::unordered_map<size_t, Face> instances_;
};
std::shared_ptr<FontManager> Fonts() {
  static auto fonts = std::make_shared<FontManager>();
  return fonts;
}
const sl::TextAttributes* Attributes(tasm::Element* node) {
  auto& attrs = node->computed_css_style()->GetTextAttributes();
  return attrs ? &*attrs : nullptr;
}
int Weight(sl::FontWeightType value) {
  if (value == sl::FontWeightType::kNormal) return 400;
  if (value == sl::FontWeightType::kBold) return 700;
  return (static_cast<int>(value) - 1) * 100;
}
struct Run {
  std::string text;
  tt::Style style;
};
tt::Style StyleFor(tasm::Element* node, const tt::Style& inherited) {
  auto style = inherited;
  auto declared = [&](tasm::CSSPropertyID id) {
    return node->GetParsedStylesMap().count(id);
  };
  auto apply = [&](tasm::CSSPropertyID id) {
    return !node->is_inline_element() || declared(id);
  };
  if (apply(tasm::kPropertyIDFontSize)) style.SetTextSize(node->GetFontSize());
  if (auto* attrs = Attributes(node)) {
    if (apply(tasm::kPropertyIDColor) && attrs->color)
      style.SetForegroundColor(*attrs->color);
    if (apply(tasm::kPropertyIDLetterSpacing))
      style.SetLetterSpacing(
          !declared(tasm::kPropertyIDLetterSpacing) &&
                  attrs->letter_spacing ==
                      sl::DefaultComputedStyle::DEFAULT_LETTER_SPACING
              ? 0
              : attrs->letter_spacing);
    auto fd = style.GetFontDescriptor();
    if (apply(tasm::kPropertyIDFontWeight)) {
      fd.font_style_.SetWeight(Weight(attrs->font_weight));
      style.SetBold(Weight(attrs->font_weight) >= 600);
    }
    if (apply(tasm::kPropertyIDFontStyle)) {
      bool italic = attrs->font_style == sl::FontStyleType::kItalic;
      style.SetItalic(italic);
      fd.font_style_.SetSlant(italic ? tt::FontStyle::kItalic_Slant
                                     : tt::FontStyle::kUpright_Slant);
    }
    if (apply(tasm::kPropertyIDFontFamily)) {
      fd.font_family_list_.clear();
      std::istringstream families(attrs->font_family.str());
      std::string family;
      while (std::getline(families, family, ',')) {
        auto first = family.find_first_not_of(" \t\"'");
        auto last = family.find_last_not_of(" \t\"'");
        if (first != std::string::npos)
          fd.font_family_list_.push_back(
              family.substr(first, last - first + 1));
      }
    }
    if (fd.font_family_list_.empty())
      fd.font_family_list_.push_back("sans-serif");
    style.SetFontDescriptor(fd);
    if (apply(tasm::kPropertyIDTextDecoration)) {
      int decoration = 0;
      if (attrs->underline_decoration)
        decoration |= int(tt::DecorationType::kUnderLine);
      if (attrs->line_through_decoration)
        decoration |= int(tt::DecorationType::kLineThrough);
      style.SetDecorationType(static_cast<tt::DecorationType>(decoration));
      style.SetDecorationColor(attrs->decoration_color
                                   ? *attrs->decoration_color
                                   : uint32_t(style.GetForegroundColor()));
    }
    if (apply(tasm::kPropertyIDWordBreak)) {
      style.SetWordBreak(attrs->word_break == sl::WordBreakType::kBreakAll
                             ? tt::WordBreakType::kBreakAll
                         : attrs->word_break == sl::WordBreakType::kKeepAll
                             ? tt::WordBreakType::kKeepAll
                             : tt::WordBreakType::kNormal);
    }
  }
  return style;
}
void Runs(tasm::Element* node, const tt::Style& parent,
          std::vector<Run>& runs) {
  if (node->is_raw_text()) {
    runs.push_back(
        {static_cast<tasm::RawTextElement*>(node)->content().str(), parent});
    return;
  }
  auto style = StyleFor(node, parent);
  if (node->is_text()) {
    const auto& content = static_cast<tasm::TextElement*>(node)->content();
    if (!content.empty()) runs.push_back({content.str(), style});
  }
  for (auto* child = node->first_child(); child; child = child->next_sibling())
    Runs(child, style, runs);
}
std::string Key(const std::vector<Run>& runs, tasm::Element* node) {
  std::ostringstream key;
  for (const auto& run : runs) {
    const auto& s = run.style;
    key << run.text.size() << ':' << run.text << ':' << s.GetTextSize() << ':'
        << uint32_t(s.GetForegroundColor()) << ':' << s.GetLetterSpacing()
        << ':' << s.GetFontDescriptor().font_style_.Value() << ':'
        << int(s.GetDecorationType()) << ':' << uint32_t(s.GetDecorationColor())
        << ':' << int(s.GetWordBreak());
    for (const auto& f : s.GetFontDescriptor().font_family_list_)
      key << ':' << f;
  }
  if (auto* a = Attributes(node))
    key << ':' << a->computed_line_height << ':' << a->line_height_factor << ':'
        << a->line_spacing << ':' << int(a->white_space) << ':'
        << int(a->text_align) << ':' << int(a->text_overflow);
  auto* props = static_cast<tasm::TextElement*>(node)->text_props();
  key << ':' << (props && props->text_max_line ? *props->text_max_line : -1);
  return key.str();
}
tt::LayoutMode Mode(int mode) {
  return mode == SLMeasureModeDefinite ? tt::LayoutMode::kDefinite
         : mode == SLMeasureModeAtMost ? tt::LayoutMode::kAtMost
                                       : tt::LayoutMode::kIndefinite;
}
int UTF16Length(const std::string& text) {
  int n = 0;
  for (unsigned char c : text)
    if (c < 0x80 || c >= 0xc0) n += c >= 0xf0 ? 2 : 1;
  return n;
}
std::vector<uint32_t> Codepoints(const std::string& text) {
  std::vector<uint32_t> result;
  for (size_t i = 0; i < text.size();) {
    uint32_t cp = static_cast<unsigned char>(text[i++]);
    if (cp >= 0xc0) {
      int extra = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : 1;
      cp &= extra == 3 ? 7 : extra == 2 ? 15 : 31;
      for (int k = 0; k < extra && i < text.size(); ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 63);
    }
    result.push_back(cp);
  }
  return result;
}
struct Layout {
  std::string key;
  std::vector<Run> runs;
  std::unique_ptr<tt::Paragraph> paragraph;
  std::unique_ptr<tt::LayoutRegion> region;
  float width = 0, height = 0, paint_offset = 0;
  int width_mode = 0, height_mode = 0;
  uint32_t glyphs = 0, missing = 0, contextual = 0, unresolved = 0;
  std::vector<uint32_t> codepoints;
  std::unordered_map<int, std::unordered_set<uint16_t>> nominal;
  std::vector<int> font_ids;
};
class Canvas final : public tt::SkityCanvasHelper {
 public:
  Canvas(skity::Canvas* canvas, Layout& layout)
      : SkityCanvasHelper(canvas), layout_(layout) {}
  void DrawGlyphs(const tt::ITypefaceHelper* face, uint32_t count,
                  const uint16_t* glyphs, const char* text, uint32_t bytes,
                  float ox, float oy, float* x, float* y,
                  tt::Painter* paint) override {
    layout_.glyphs += count;
    for (uint32_t i = 0; i < count; ++i)
      if (!glyphs[i]) ++layout_.missing;
    auto id = static_cast<const skity::textlayout::SkityTypefaceHelper*>(face)
                  ->GetTypeface()
                  ->TypefaceId();
    auto entry = layout_.nominal.find(id);
    if (entry == layout_.nominal.end()) {
      std::unordered_set<uint16_t> nominal;
      for (auto cp : layout_.codepoints)
        nominal.insert(face->UnicharToGlyph(cp));
      entry = layout_.nominal.emplace(id, std::move(nominal)).first;
    }
    for (uint32_t i = 0; i < count; ++i)
      if (glyphs[i] && !entry->second.count(glyphs[i])) ++layout_.contextual;
    if (std::find(layout_.font_ids.begin(), layout_.font_ids.end(), id) ==
        layout_.font_ids.end())
      layout_.font_ids.push_back(id);
    SkityCanvasHelper::DrawGlyphs(face, count, glyphs, text, bytes, ox, oy, x,
                                  y, paint);
  }

 private:
  Layout& layout_;
};
}  // namespace
struct BrowserText::Impl {
  bool active = false;
  tt::FontmgrCollection fonts{Fonts()};
  tt::TextLayout engine{&fonts, tt::kSelfRendering};
  std::unordered_map<int, Layout> layouts;
  Layout& Prepare(tasm::Element* node, float width, int wm, float height,
                  int hm) {
    FontRequestScope requests(active);
    tt::Style base;
    base.SetTextSize(node->GetFontSize());
    base.SetForegroundColor(0xff000000);
    std::vector<Run> runs;
    Runs(node, base, runs);
    for (const auto& run : runs) {
      for (auto cp : Codepoints(run.text)) Fonts()->Ensure(cp);
      for (const auto& family : run.style.GetFontDescriptor().font_family_list_)
        Fonts()->EnsureFamily(family.c_str());
    }
    auto key = Key(runs, node);
    auto& cache = layouts[node->impl_id()];
    if (cache.region && cache.key == key && cache.width == width &&
        cache.height == height && cache.width_mode == wm &&
        cache.height_mode == hm)
      return cache;
    // Lines retain paragraph run pointers; release the region first.
    cache.region.reset();
    cache = {};
    cache.key = std::move(key);
    cache.runs = std::move(runs);
    std::string content;
    for (const auto& run : cache.runs) content += run.text;
    cache.codepoints = Codepoints(content);
    for (auto cp : cache.codepoints)
      if (!Fonts()->Supports(cp)) ++cache.unresolved;
    cache.width = width;
    cache.height = height;
    cache.width_mode = wm;
    cache.height_mode = hm;
    cache.paragraph = tt::Paragraph::Create();
    auto& ps = cache.paragraph->GetParagraphStyle();
    ps.SetDefaultStyle(StyleFor(node, base));
    if (auto* a = Attributes(node)) {
      switch (a->text_align) {
        case sl::TextAlignType::kCenter:
          ps.SetHorizontalAlign(tt::ParagraphHorizontalAlignment::kCenter);
          break;
        case sl::TextAlignType::kRight:
          ps.SetHorizontalAlign(tt::ParagraphHorizontalAlignment::kRight);
          break;
        case sl::TextAlignType::kStart:
          ps.SetHorizontalAlign(tt::ParagraphHorizontalAlignment::kStart);
          break;
        case sl::TextAlignType::kEnd:
          ps.SetHorizontalAlign(tt::ParagraphHorizontalAlignment::kEnd);
          break;
        case sl::TextAlignType::kJustify:
          ps.SetHorizontalAlign(tt::ParagraphHorizontalAlignment::kJustify);
          break;
        default:
          break;
      }
      if (a->computed_line_height > 0)
        ps.SetLineHeightInPx(a->computed_line_height, tt::RulerType::kExact);
      else if (a->line_height_factor > 0)
        ps.SetLineHeightInPx(node->GetFontSize() * a->line_height_factor,
                             tt::RulerType::kExact);
      if (a->line_spacing != sl::DefaultComputedStyle::DEFAULT_LINE_SPACING)
        ps.SetLineSpaceAfterPx(a->line_spacing);
      if (a->white_space == sl::WhiteSpaceType::kNowrap) ps.SetMaxLines(1);
      if (a->text_overflow == sl::TextOverflowType::kEllipsis)
        ps.SetEllipsis("…");
      else
        ps.SetEllipsis("");
    }
    auto* props = static_cast<tasm::TextElement*>(node)->text_props();
    if (props && props->text_max_line && *props->text_max_line > 0)
      ps.SetMaxLines(*props->text_max_line);
    bool nowrap_clip = false;
    const auto* attributes = Attributes(node);
    if (attributes && attributes->white_space == sl::WhiteSpaceType::kNowrap) {
      ps.SetMaxLines(1);
      nowrap_clip = attributes->text_overflow == sl::TextOverflowType::kClip;
      if (nowrap_clip)
        ps.SetHorizontalAlign(tt::ParagraphHorizontalAlignment::kLeft);
    }
    ps.SetOverflowWrap(tt::OverflowWrap::kBreakWord);
    for (const auto& run : cache.runs)
      cache.paragraph->AddTextRun(&run.style, run.text.data(), run.text.size());
    cache.region = std::make_unique<tt::LayoutRegion>(
        wm == SLMeasureModeIndefinite || nowrap_clip ? 1000000.f
                                                     : std::max(width, 0.f),
        hm == SLMeasureModeIndefinite ? 1000000.f : std::max(height, 0.f),
        nowrap_clip ? tt::LayoutMode::kIndefinite : Mode(wm), Mode(hm));
    tt::TTTextContext context;
    engine.Layout(cache.paragraph.get(), cache.region.get(), context);
    if (nowrap_clip && wm != SLMeasureModeIndefinite) {
      auto remaining = width - cache.region->GetLayoutedWidth();
      if (attributes->text_align == sl::TextAlignType::kCenter)
        cache.paint_offset = remaining / 2;
      else if (attributes->text_align == sl::TextAlignType::kRight)
        cache.paint_offset = remaining;
    }
    return cache;
  }
};
BrowserText::BrowserText() : impl_(std::make_unique<Impl>()) {}
BrowserText::~BrowserText() = default;
void BrowserText::Activate() { impl_->active = true; }
bool BrowserText::RegisterFont(int slot, const uint8_t* bytes, size_t size) {
  return Fonts()->Register(slot, bytes, size);
}
void BrowserText::InvalidateFonts() {
  impl_->layouts.clear();
  impl_->fonts.clearCaches();
  tt::TextLayout::ClearShapeCache();
}
tasm::LayoutResult BrowserText::Measure(tasm::Element* element, float width,
                                        int wm, float height, int hm) {
  auto& c = impl_->Prepare(element, width, wm, height, hm);
  auto& r = *c.region;
  auto* node = static_cast<tasm::TextElement*>(element);
  auto lines = std::make_unique<tasm::TextLineInfo[]>(r.GetLineCount());
  for (uint32_t i = 0; i < r.GetLineCount(); ++i) {
    auto* line = r.GetLine(i);
    lines[i].start =
        UTF16Length(c.paragraph->GetContentString(0, line->GetStartCharPos()));
    lines[i].end =
        UTF16Length(c.paragraph->GetContentString(0, line->GetEndCharPos()));
    if (i + 1 == r.GetLineCount() && r.DidExceedMaxLines() &&
        (!c.paragraph->GetParagraphStyle().GetEllipsis().empty() ||
         c.paragraph->GetParagraphStyle().GetEllipsisDelegate()))
      lines[i].ellipsis_count = 1;
  }
  node->SetTextLineLayoutInfo(std::move(lines), r.GetLineCount());
  node->SetTextLayoutEventSize({r.GetLayoutedWidth(), r.GetLayoutedHeight()});
  float measured_width =
      wm == SLMeasureModeDefinite ? width : r.GetLayoutedWidth();
  float measured_height =
      hm == SLMeasureModeDefinite ? height : r.GetLayoutedHeight();
  if (wm == SLMeasureModeAtMost)
    measured_width = std::min(measured_width, width);
  if (hm == SLMeasureModeAtMost)
    measured_height = std::min(measured_height, height);
  return {measured_width, measured_height,
          r.GetLineCount() ? r.GetLine(0)->GetLineBaseLine() : 0};
}
void BrowserText::Align(tasm::Element* node) {
  if (!node->slnode()) return;
  auto width = node->slnode()->GetContentBoundWidth();
  // Line geometry and painting share the final content width. Height remains
  // unconstrained here; the text's viewport clips the already-laid-out glyphs.
  impl_->Prepare(node, std::max(width, 0.f), SLMeasureModeDefinite, 0,
                 SLMeasureModeIndefinite);
}
void BrowserText::Paint(skity::Canvas* canvas, tasm::Element* node, float x,
                        float y) {
  if (node->is_inline_element()) return;
  Align(node);
  auto& c = impl_->layouts.at(node->impl_id());
  const auto& b = node->slnode()->GetLayoutResult();
  auto left = b.padding_[sl::kLeft] + b.border_[sl::kLeft];
  auto top = b.padding_[sl::kTop] + b.border_[sl::kTop];
  canvas->Save();
  canvas->Translate(x + left, y + top);
  canvas->ClipRect(
      skity::Rect::MakeWH(node->slnode()->GetContentBoundWidth(),
                          node->slnode()->GetContentBoundHeight()));
  c.glyphs = 0;
  c.contextual = 0;
  c.missing = 0;
  c.font_ids.clear();
  canvas->Translate(c.paint_offset, 0);
  Canvas helper(canvas, c);
  tt::LayoutDrawer drawer(&helper);
  drawer.DrawLayoutPage(c.region.get());
  canvas->Restore();
}
void BrowserText::Erase(tasm::Element* node) {
  impl_->layouts.erase(node->impl_id());
}
void BrowserText::Report(tasm::Element* node,
                         rapidjson::Writer<rapidjson::StringBuffer>& w) const {
  auto found = impl_->layouts.find(node->impl_id());
  if (found == impl_->layouts.end() || !found->second.region) return;
  const auto& c = found->second;
  auto& r = *c.region;
  w.Key("textLayout");
  w.StartObject();
  w.Key("engine");
  w.String("tttext-harfbuzz");
  w.Key("width");
  w.Double(r.GetLayoutedWidth());
  w.Key("height");
  w.Double(r.GetLayoutedHeight());
  w.Key("glyphs");
  w.Uint(c.glyphs);
  w.Key("missingGlyphs");
  w.Uint(std::max(c.missing, c.unresolved));
  w.Key("contextualGlyphs");
  w.Uint(c.contextual);
  w.Key("fontCount");
  w.Uint(c.font_ids.size());
  w.Key("charCount");
  w.Uint(c.paragraph->GetCharCount());
  w.Key("exceededMaxLines");
  w.Bool(r.DidExceedMaxLines());
  w.Key("runs");
  w.StartArray();
  for (const auto& run : c.runs) {
    w.StartObject();
    w.Key("text");
    w.String(run.text.c_str());
    w.Key("color");
    w.Uint(uint32_t(run.style.GetForegroundColor()));
    w.Key("size");
    w.Double(run.style.GetTextSize());
    w.Key("weight");
    w.Int(run.style.GetFontDescriptor().font_style_.GetWeight());
    w.EndObject();
  }
  w.EndArray();
  w.Key("lines");
  w.StartArray();
  for (uint32_t i = 0; i < r.GetLineCount(); ++i) {
    auto* line = r.GetLine(i);
    w.StartObject();
    w.Key("start");
    w.Uint(line->GetStartCharPos());
    w.Key("end");
    w.Uint(line->GetEndCharPos());
    w.Key("x");
    w.Double(line->GetLineLeft() + c.paint_offset);
    w.Key("y");
    w.Double(line->GetLineTop());
    w.Key("width");
    w.Double(line->GetLineWidth());
    w.Key("height");
    w.Double(line->GetLineHeight());
    w.Key("baseline");
    w.Double(line->GetLineBaseLine());
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
}
}  // namespace lynx::wasm
