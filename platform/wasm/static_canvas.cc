// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <emscripten.h>
#include <webgpu/webgpu.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <skity/gpu/gpu_context_web.hpp>
#include <skity/text/typeface.hpp>
#include "core/renderer/starlight/layout/layout_object.h"
#include "core/renderer/starlight/style/layout_computed_style.h"
#include "quickjs/include/quickjs.h"
#include "rapidjson/document.h"

namespace {
namespace sl = lynx::starlight;

struct SceneNode {
  explicit SceneNode(const sl::LayoutConfigs& config)
      : style(1.f), layout(config, &style) {}
  sl::LayoutComputedStyle style;
  sl::LayoutObject layout;
  std::string text;
  float text_size = 20;
  skity::Color background = 0;
  std::vector<std::unique_ptr<SceneNode>> children;
};

sl::LayoutConfigs layout_config;
std::unique_ptr<SceneNode> root;
std::unique_ptr<skity::GPUContext> gpu;
std::vector<std::unique_ptr<skity::GPUSurface>> pending_surfaces;
std::shared_ptr<skity::Typeface> font;
WGPUDevice device = nullptr;
WGPUQueue queue = nullptr;
WGPUSurface surface = nullptr;
std::string report;
int node_count = 0;
int frame_count = 0;

float Number(const rapidjson::Value& value, const char* key, float fallback) {
  return value.HasMember(key) && value[key].IsNumber()
             ? value[key].GetFloat() : fallback;
}

skity::Color ParseColor(const char* text) {
  if (!text || text[0] != '#' || std::char_traits<char>::length(text) != 7)
    return 0;
  return 0xff000000u | static_cast<uint32_t>(std::strtoul(text + 1, nullptr, 16));
}

std::unique_ptr<SceneNode> MakeNode(const rapidjson::Value& value) {
  auto node = std::make_unique<SceneNode>(layout_config);
  ++node_count;
  node->style.SetDisplay(sl::DisplayType::kFlex);
  node->style.SetFlexShrink(0.f);
  if (value.HasMember("width"))
    node->style.SetWidth(sl::NLength::MakeUnitNLength(Number(value, "width", 0)));
  if (value.HasMember("height"))
    node->style.SetHeight(sl::NLength::MakeUnitNLength(Number(value, "height", 0)));
  const bool row = value.HasMember("direction") &&
                  std::string(value["direction"].GetString()) == "row";
  node->style.SetFlexDirection(row ? sl::FlexDirectionType::kRow
                                  : sl::FlexDirectionType::kColumn);
  const auto padding = sl::NLength::MakeUnitNLength(Number(value, "padding", 0));
  node->style.SetPaddingLeft(padding);
  node->style.SetPaddingRight(padding);
  node->style.SetPaddingTop(padding);
  node->style.SetPaddingBottom(padding);
  if (value.HasMember("background"))
    node->background = ParseColor(value["background"].GetString());
  if (value.HasMember("text")) node->text = value["text"].GetString();
  node->text_size = Number(value, "size", 20);
  if (value.HasMember("children")) {
    const float gap = Number(value, "gap", 0);
    const auto& children = value["children"];
    for (rapidjson::SizeType i = 0; i < children.Size(); ++i) {
      auto child = MakeNode(children[i]);
      if (i + 1 < children.Size()) {
        if (row) child->style.SetMarginRight(sl::NLength::MakeUnitNLength(gap));
        else child->style.SetMarginBottom(sl::NLength::MakeUnitNLength(gap));
      }
      node->layout.AppendChild(&child->layout);
      node->children.push_back(std::move(child));
    }
  }
  return node;
}

void PaintNode(skity::Canvas* canvas, SceneNode& node, float parent_x,
               float parent_y) {
  const auto& bounds = node.layout.GetLayoutResult();
  const float x = parent_x + bounds.offset_.X();
  const float y = parent_y + bounds.offset_.Y();
  skity::Paint paint;
  if (node.background) {
    paint.SetColor(node.background);
    canvas->DrawRoundRect(skity::Rect::MakeXYWH(x, y, bounds.size_.width_,
                                              bounds.size_.height_),
                          &node == root.get() ? 0 : 12, 12, paint);
  }
  if (!node.text.empty()) {
    paint.SetColor(0xffeaf2ff);
    paint.SetTypeface(font);
    paint.SetTextSize(node.text_size);
    canvas->DrawSimpleText(node.text.c_str(), x + 16,
                           y + 16 + node.text_size, paint);
  }
  for (auto& child : node.children) PaintNode(canvas, *child, x, y);
}

bool LoadScene() {
  std::ifstream input("/scene.js");
  const std::string script((std::istreambuf_iterator<char>(input)), {});
  auto* runtime = LEPUS_NewRuntime();
  if (!runtime) return false;
  auto* context = LEPUS_NewContext(runtime);
  if (!context || script.empty()) {
    if (context) LEPUS_FreeContext(context);
    LEPUS_FreeRuntime(runtime);
    return false;
  }
  const auto value = LEPUS_Eval(context, script.c_str(), script.size(),
                               "scene.js", LEPUS_EVAL_TYPE_GLOBAL);
  if (LEPUS_IsException(value)) {
    const auto error = LEPUS_GetException(context);
    const char* text = LEPUS_ToCString(context, error);
    std::fprintf(stderr, "QuickJS error: %s\n", text ? text : "unknown");
    if (text) LEPUS_FreeCString(context, text);
    LEPUS_FreeValue(context, error);
    LEPUS_FreeContext(context);
    LEPUS_FreeRuntime(runtime);
    return false;
  }
  const char* json = LEPUS_ToCString(context, value);
  rapidjson::Document document;
  document.Parse(json ? json : "");
  LEPUS_FreeCString(context, json);
  LEPUS_FreeValue(context, value);
  LEPUS_FreeContext(context);
  LEPUS_FreeRuntime(runtime);
  if (document.HasParseError() || !document.IsObject()) return false;
  layout_config.SetQuirksMode(lynx::base::Version(3, 1));
  root = MakeNode(document);
  root->layout.ReLayout();
  return true;
}
}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE const char* scene_report() {
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
  if (!target) { wgpuTextureRelease(current.texture); return 3; }
  // At the pinned Skity revision, GPUSurfaceImplWEB takes ownership of the
  // supplied texture and releases it in its destructor without adding a ref.
  auto* canvas = target->LockCanvas();
  if (!canvas) return 4;
  PaintNode(canvas, *root, 0, 0);
  canvas->Flush();
  target->Flush();
  pending_surfaces.push_back(std::move(target));
  ++frame_count;
  const auto& bounds = root->layout.GetLayoutResult();
  char output[256];
  std::snprintf(output, sizeof(output),
                "{\"qjs\":true,\"layout\":\"lynx-starlight\","
                "\"renderer\":\"skity-webgpu\",\"nodes\":%d,\"frames\":%d,"
                "\"width\":%g,\"height\":%g,\"fullLynxBundle\":false}",
                node_count, frame_count, bounds.size_.width_, bounds.size_.height_);
  report = output;
  std::puts(report.c_str());
  return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE void release_completed_frames() {
  pending_surfaces.clear();
}

int main() {
  if (!LoadScene()) return 1;
  device = emscripten_webgpu_get_device();
  if (!device) return 2;
  queue = wgpuDeviceGetQueue(device);
  gpu = skity::WebContextCreate(device, queue);
  font = skity::Typeface::MakeFromFile("/font.ttf");
  if (!gpu || !font) return 3;
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
