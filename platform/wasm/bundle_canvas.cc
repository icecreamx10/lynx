// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <emscripten.h>
#include <webgpu/webgpu.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <numeric>
#include <skity/gpu/gpu_context_web.hpp>
#include <skity/text/font.hpp>
#include <skity/text/typeface.hpp>
#include <string>
#include <vector>

#include "core/base/threading/task_runner_manufactor.h"
#include "core/renderer/dom/fiber/page_element.h"
#include "core/renderer/dom/fiber/raw_text_element.h"
#include "core/renderer/dom/fiber/text_element.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "core/renderer/starlight/style/layout_computed_style.h"
#include "core/shell/runtime/mts/mts_runtime.h"
#include "platform/wasm/static_bundle_host.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace {
namespace sl = lynx::starlight;

namespace tasm = lynx::tasm;
std::unique_ptr<tasm::StaticBundleDelegate> delegate;
std::string load_error;
int bundle_sequence = 0;
int instance_id = 10;
std::unique_ptr<tasm::TemplateAssembler> assembler;
tasm::ElementManager* manager = nullptr;
tasm::Element* root = nullptr;
size_t bundle_bytes = 0;
bool qjs_verified = false;
std::unique_ptr<skity::GPUContext> gpu;
std::vector<std::unique_ptr<skity::GPUSurface>> pending_surfaces;
std::shared_ptr<skity::Typeface> font;
WGPUDevice device = nullptr;
WGPUQueue queue = nullptr;
WGPUSurface surface = nullptr;
std::string report;
int node_count = 0;
int frame_count = 0;

std::string TextContent(tasm::Element* node) {
  if (node->is_raw_text())
    return static_cast<tasm::RawTextElement*>(node)->content().str();
  if (node->is_text()) {
    const auto& content = static_cast<tasm::TextElement*>(node)->content();
    if (!content.empty()) return content.str();
  }
  std::string text;
  for (auto* child = node->first_child(); child; child = child->next_sibling())
    text += TextContent(child);
  return text;
}

void PaintNode(skity::Canvas* canvas, tasm::Element& node, float parent_x,
               float parent_y) {
  if (!node.slnode()) return;
  const auto& bounds = node.slnode()->GetLayoutResult();
  const float x = parent_x + bounds.offset_.X();
  const float y = parent_y + bounds.offset_.Y();
  skity::Paint paint;
  auto* style = node.computed_css_style();
  auto background =
      style->ExtractCanonicalComputedValue(tasm::kPropertyIDBackgroundColor);
  if (background &&
      background->kind() == sl::CanonicalComputedValue::Kind::kColor) {
    paint.SetColor(std::get<uint32_t>(background->storage()));
    canvas->DrawRect(
        skity::Rect::MakeXYWH(x, y, bounds.size_.width_, bounds.size_.height_),
        paint);
  }
  if (node.is_text()) {
    const std::string text = TextContent(&node);
    auto color = style->ExtractCanonicalComputedValue(tasm::kPropertyIDColor);
    paint.SetColor(color && color->kind() ==
                                sl::CanonicalComputedValue::Kind::kColor
                       ? std::get<uint32_t>(color->storage())
                       : 0xff000000);
    paint.SetTypeface(font);
    paint.SetTextSize(node.GetFontSize());
    skity::FontMetrics metrics{};
    skity::Font(font, node.GetFontSize()).GetMetrics(&metrics);
    canvas->DrawSimpleText(text.c_str(), x, y - metrics.ascent_, paint);
  }
  for (auto* child = node.first_render_child(); child;
       child = child->next_render_sibling())
    PaintNode(canvas, *child, x, y);
}

int CountNodes(tasm::Element* node) {
  int count = 1;
  std::printf("element %s text=%s\n", node->GetTag().c_str(),
              TextContent(node).c_str());
  for (auto* child = node->first_child(); child; child = child->next_sibling())
    count += CountNodes(child);
  return count;
}

bool ValidateStaticTree(tasm::Element* node, std::string& error) {
  const auto tag = node->GetTag().str();
  if (tag != "page" && tag != "view" && tag != "text" && tag != "raw-text") {
    error = "This static canvas host does not support element: " + tag;
    return false;
  }
  if (node->is_text()) {
    for (unsigned char c : TextContent(node)) {
      if (c >= 128 || c == '\n') {
        error = "This prototype currently supports single-line ASCII text only";
        return false;
      }
    }
  }
  for (auto* child = node->first_child(); child; child = child->next_sibling())
    if (!ValidateStaticTree(child, error)) return false;
  return true;
}

bool LoadBundle(std::vector<uint8_t> bytes) {
  load_error.clear();
  if (bytes.empty()) {
    load_error = "The bundle is empty";
    return false;
  }
  const size_t size = bytes.size();
  auto candidate_delegate = std::make_unique<tasm::StaticBundleDelegate>();
  const int id = ++instance_id;
  tasm::PageOptions options(id);
  options.SetEmbeddedMode(static_cast<tasm::EmbeddedMode>(
      tasm::EMBEDDED_MODE_BASE | tasm::LAYOUT_IN_ELEMENT));
  options.SetLongTaskMonitorDisabled(true);
  auto elements = std::make_unique<tasm::ElementManager>(
      std::make_unique<tasm::CanvasPaintingContext>(candidate_delegate->errors),
      candidate_delegate.get(), tasm::LynxEnvConfig(800, 500, 1.f, 1.f),
      options);
  auto* candidate_manager = elements.get();
  elements->SetPropBundleCreator(
      std::make_shared<tasm::CanvasPropBundleCreator>());
  auto candidate = std::make_unique<tasm::TemplateAssembler>(
      *candidate_delegate, std::move(elements), nullptr, id, false, options);
  candidate_manager->UpdateViewport(800, SLMeasureModeDefinite, 500,
                                    SLMeasureModeDefinite, false);
  auto pipeline = std::make_shared<tasm::PipelineOptions>();
  candidate->LoadTemplate("local.lynx.bundle", std::move(bytes),
                          std::make_shared<tasm::TemplateData>(), pipeline);
  auto* candidate_root = candidate_manager->GetPageElement();
  if (!candidate_delegate->errors.empty()) {
    load_error = candidate_delegate->errors.front();
    return false;
  }
  if (!candidate_delegate->loaded || !candidate_root) {
    load_error = "Lynx did not produce a page from this bundle";
    return false;
  }
  const auto runtime = candidate->GetLepusContext(tasm::DEFAULT_ENTRY_NAME);
  if (!runtime || !runtime->IsLepusNGContext()) {
    load_error = "This browser host requires a LepusNG/QuickJS bundle";
    return false;
  }
  if (!ValidateStaticTree(candidate_root, load_error)) return false;
  const int count = CountNodes(candidate_root);
  if (count <= 1) {
    load_error = "The bundle did not produce any visible elements";
    return false;
  }
  // Validate before replacing the displayed page. Keep the retired delegate
  // alive until its TemplateAssembler has been destroyed.
  auto retired_delegate = std::move(delegate);
  auto retired_assembler = std::move(assembler);
  delegate = std::move(candidate_delegate);
  assembler = std::move(candidate);
  manager = candidate_manager;
  root = candidate_root;
  bundle_bytes = size;
  node_count = count;
  qjs_verified = true;
  frame_count = 0;
  ++bundle_sequence;
  retired_assembler.reset();
  retired_delegate.reset();
  return true;
}

bool LoadScene() {
  lynx::fml::MessageLoop::EnsureInitializedForCurrentThread();
  lynx::base::UIThread::Init();
  std::ifstream input("/main.lynx.bundle", std::ios::binary);
  return LoadBundle(
      std::vector<uint8_t>((std::istreambuf_iterator<char>(input)), {}));
}
}  // namespace

lynx::tasm::LayoutResult lynx::tasm::CanvasPaintingContext::MeasureText(
    Element* element, float width, int width_mode, float height,
    int height_mode) {
  std::string text = TextContent(element);
  std::vector<skity::GlyphID> glyphs;
  for (unsigned char c : text) {
    if (c >= 128 || c == '\n') {
      errors_.push_back(
          "Only single-line ASCII text is supported by the canvas prototype");
      return {};
    }
    glyphs.push_back(font->UnicharToGlyph(c));
  }
  skity::Font text_font(font, element->GetFontSize());
  std::vector<float> widths(glyphs.size());
  text_font.GetWidths(glyphs.data(), glyphs.size(), widths.data());
  skity::FontMetrics metrics{};
  text_font.GetMetrics(&metrics);
  float measured_width = std::accumulate(widths.begin(), widths.end(), 0.f);
  float measured_height = metrics.descent_ - metrics.ascent_;
  return {width_mode == SLMeasureModeDefinite ? width : measured_width,
          height_mode == SLMeasureModeDefinite ? height : measured_height,
          -metrics.ascent_};
}

namespace {
void ReportElements(rapidjson::Writer<rapidjson::StringBuffer>& writer,
                    tasm::Element* node, float parent_x, float parent_y) {
  float x = parent_x, y = parent_y;
  writer.StartObject();
  writer.Key("tag");
  writer.String(node->GetTag().c_str());
  if (node->is_text()) {
    writer.Key("text");
    writer.String(TextContent(node).c_str());
    writer.Key("fontSize");
    writer.Double(node->GetFontSize());
  }
  if (node->slnode()) {
    const auto& bounds = node->slnode()->GetLayoutResult();
    x += bounds.offset_.X();
    y += bounds.offset_.Y();
    writer.Key("x");
    writer.Double(x);
    writer.Key("y");
    writer.Double(y);
    writer.Key("width");
    writer.Double(bounds.size_.width_);
    writer.Key("height");
    writer.Double(bounds.size_.height_);
  }
  writer.Key("children");
  writer.StartArray();
  for (auto* child = node->first_child(); child; child = child->next_sibling())
    ReportElements(writer, child, x, y);
  writer.EndArray();
  writer.EndObject();
}
}  // namespace
extern "C" EMSCRIPTEN_KEEPALIVE const char* scene_report() {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("qjs");
  writer.Bool(qjs_verified);
  writer.Key("layout");
  writer.String("lynx-starlight");
  writer.Key("renderer");
  writer.String("skity-webgpu");
  writer.Key("nodes");
  writer.Int(node_count);
  writer.Key("frames");
  writer.Int(frame_count);
  writer.Key("width");
  writer.Int(800);
  writer.Key("height");
  writer.Int(500);
  writer.Key("fullLynxBundle");
  writer.Bool(true);
  writer.Key("bundleLoaded");
  writer.Bool(delegate->loaded);
  writer.Key("backgroundRuntime");
  writer.Bool(false);
  writer.Key("backgroundSourcePrepared");
  writer.Bool(delegate->background_source_prepared);
  writer.Key("bundleSequence");
  writer.Int(bundle_sequence);
  writer.Key("bundleBytes");
  writer.Uint64(bundle_bytes);
  writer.Key("errors");
  writer.StartArray();
  for (const auto& error : delegate->errors) writer.String(error.c_str());
  writer.EndArray();
  if (root) {
    writer.Key("tree");
    ReportElements(writer, root, 0, 0);
  }
  writer.EndObject();
  report = buffer.GetString();
  return report.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE int render_scene() {
  if (!gpu || !root) return 1;
  WGPUSurfaceTexture current = WGPU_SURFACE_TEXTURE_INIT;
  wgpuSurfaceGetCurrentTexture(surface, &current);
  if (!current.texture) return 2;
  skity::GPUSurfaceDescriptorWEB descriptor;
  descriptor.backend = skity::GPUBackendType::kWebGPU;
  descriptor.width = 800;
  descriptor.height = 500;
  descriptor.texture = current.texture;
  auto target = gpu->CreateSurface(&descriptor);
  if (!target) {
    wgpuTextureRelease(current.texture);
    return 3;
  }
  // At the pinned Skity revision, GPUSurfaceImplWEB takes ownership of the
  // supplied texture and releases it in its destructor without adding a ref.
  auto* canvas = target->LockCanvas();
  if (!canvas) return 4;
  PaintNode(canvas, *root, 0, 0);
  canvas->Flush();
  target->Flush();
  pending_surfaces.push_back(std::move(target));
  ++frame_count;
  return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE void release_completed_frames() {
  pending_surfaces.clear();
}

extern "C" EMSCRIPTEN_KEEPALIVE int load_bundle(const uint8_t* bytes,
                                                int size) {
  if (!bytes || size <= 0 || size > 64 * 1024 * 1024) {
    load_error = "Select a non-empty compiled bundle smaller than 64 MiB";
    return 1;
  }
  if (!pending_surfaces.empty()) {
    load_error = "Wait for the previous GPU frame to finish before loading";
    return 2;
  }
  return LoadBundle(std::vector<uint8_t>(bytes, bytes + size)) ? 0 : 3;
}
extern "C" EMSCRIPTEN_KEEPALIVE const char* bundle_load_error() {
  return load_error.c_str();
}

// Validation-only entry point: exercise the same loader with an invalid binary
// and a separate instance, leaving the displayed page intact.
extern "C" EMSCRIPTEN_KEEPALIVE int verify_corrupt_bundle() {
  tasm::StaticBundleDelegate invalid_delegate;
  tasm::PageOptions options(2);
  options.SetEmbeddedMode(static_cast<tasm::EmbeddedMode>(
      tasm::EMBEDDED_MODE_BASE | tasm::LAYOUT_IN_ELEMENT));
  options.SetLongTaskMonitorDisabled(true);
  auto elements = std::make_unique<tasm::ElementManager>(
      std::make_unique<tasm::CanvasPaintingContext>(invalid_delegate.errors),
      &invalid_delegate, tasm::LynxEnvConfig(800, 500, 1.f, 1.f), options);
  elements->SetPropBundleCreator(
      std::make_shared<tasm::CanvasPropBundleCreator>());
  tasm::TemplateAssembler invalid_assembler(
      invalid_delegate, std::move(elements), nullptr, 2, false, options);
  auto pipeline = std::make_shared<tasm::PipelineOptions>();
  invalid_assembler.LoadTemplate(
      "invalid.lynx.bundle", {'i', 'n', 'v', 'a', 'l', 'i', 'd', '!'},
      std::make_shared<tasm::TemplateData>(), pipeline);
  return !invalid_delegate.loaded && !invalid_delegate.errors.empty() ? 0 : 1;
}

int main() {
  font = skity::Typeface::MakeFromFile("/font.ttf");
  if (!font) return 3;
  if (!LoadScene()) return 1;
  device = emscripten_webgpu_get_device();
  if (!device) return 2;
  queue = wgpuDeviceGetQueue(device);
  gpu = skity::WebContextCreate(device, queue);
  if (!gpu) return 3;
  auto instance = wgpuCreateInstance(nullptr);
  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector selector =
      WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
  selector.selector = {"#canvas", WGPU_STRLEN};
  WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
  descriptor.nextInChain = &selector.chain;
  surface = wgpuInstanceCreateSurface(instance, &descriptor);
  WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
  configuration.device = device;
  configuration.format = WGPUTextureFormat_BGRA8Unorm;
  configuration.usage = WGPUTextureUsage_RenderAttachment;
  configuration.width = 800;
  configuration.height = 500;
  wgpuSurfaceConfigure(surface, &configuration);
  wgpuInstanceRelease(instance);
  return render_scene();
}
