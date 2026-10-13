// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include "platform/wasm/browser_images.h"

#include <emscripten.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "base/include/value/base_value.h"
EM_JS(void, RequestBrowserImage,
      (int token, const char* src, int autoplay, int loops), {
        Module.lynxImages.request(token, UTF8ToString(src), !!autoplay, loops);
      });
EM_JS(void, CancelBrowserImage, (int token),
      { Module.lynxImages.cancel(token); });
EM_JS(void, ConfigureBrowserImage, (int token, int autoplay, int loops),
      { Module.lynxImages.configure(token, !!autoplay, loops); });
EM_JS(void, TickBrowserImages, (double time),
      { Module.lynxImages.tick(time); });
namespace lynx::wasm {
BrowserImages::BrowserImages(skity::GPUContext* gpu, WGPUDevice device,
                             Event event, std::function<void()> invalidate)
    : gpu_(gpu),
      device_(device),
      event_(std::move(event)),
      invalidate_(std::move(invalidate)) {}
BrowserImages::~BrowserImages() { Reset(); }
BrowserImages::Record::~Record() {
  image.reset();
  if (texture) wgpuTextureRelease(texture);
}
void BrowserImages::Reset() {
  for (const auto& [id, r] : records_) CancelBrowserImage(r->token);
  records_.clear();
}
BrowserImages::Record* BrowserImages::Find(int token) {
  for (auto& [id, r] : records_)
    if (r->token == token) return r.get();
  return nullptr;
}
void BrowserImages::Sync(tasm::Element* root) {
  std::unordered_set<int> present;
  std::function<void(tasm::Element*)> visit = [&](tasm::Element* node) {
    if (node->is_image()) {
      auto* image = static_cast<tasm::ImageElement*>(node);
      const int id = node->impl_id();
      const auto src = image->src().str();
      if (!src.empty()) {
        present.insert(id);
        auto& r = records_[id];
        if (!r || r->src != src) {
          if (r) CancelBrowserImage(r->token);
          r = std::make_shared<Record>();
          r->node = id;
          r->token = ++next_token_;
          r->src = src;
          r->autoplay = image->paint_info().autoplay;
          r->loop_count = std::max(0, image->paint_info().loop_count);
          RequestBrowserImage(r->token, src.c_str(), r->autoplay,
                              r->loop_count);
        } else if (r->autoplay != image->paint_info().autoplay ||
                   r->loop_count !=
                       std::max(0, image->paint_info().loop_count)) {
          r->autoplay = image->paint_info().autoplay;
          r->loop_count = std::max(0, image->paint_info().loop_count);
          ConfigureBrowserImage(r->token, r->autoplay, r->loop_count);
        }
      }
    }
    for (auto* child = node->first_child(); child;
         child = child->next_sibling())
      visit(child);
  };
  if (root) visit(root);
  for (auto it = records_.begin(); it != records_.end();) {
    if (!present.count(it->first)) {
      CancelBrowserImage(it->second->token);
      it = records_.erase(it);
    } else
      ++it;
  }
}
void BrowserImages::Tick(double time) { TickBrowserImages(time); }
WGPUTexture BrowserImages::Allocate(int token, int width, int height) {
  auto* r = Find(token);
  if (!r || width <= 0 || height <= 0 ||
      static_cast<int64_t>(width) * height > 16 * 1024 * 1024)
    return nullptr;
  if (r->texture)
    return r->width == width && r->height == height ? r->texture : nullptr;
  WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
  desc.dimension = WGPUTextureDimension_2D;
  desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
  desc.format = WGPUTextureFormat_RGBA8Unorm;
  desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
               WGPUTextureUsage_RenderAttachment;
  desc.mipLevelCount = 1;
  desc.sampleCount = 1;
  r->texture = wgpuDeviceCreateTexture(device_, &desc);
  if (!r->texture) return nullptr;
  skity::GPUBackendTextureInfoWEB info;
  info.backend = skity::GPUBackendType::kWebGPU;
  info.width = width;
  info.height = height;
  info.format = skity::TextureFormat::kRGBA;
  info.texture = r->texture;
  auto texture = gpu_->WrapTexture(&info);
  if (!texture) {
    wgpuTextureRelease(r->texture);
    r->texture = nullptr;
    return nullptr;
  }
  r->image = skity::Image::MakeHWImage(std::move(texture));
  r->width = width;
  r->height = height;
  return r->texture;
}
void BrowserImages::Complete(int token, const std::string& error) {
  auto* r = Find(token);
  if (!r || r->failed) return;
  auto params = lepus::Dictionary::Create();
  if (!error.empty()) {
    r->failed = true;
    r->error = error;
    r->image.reset();
    if (r->texture) {
      wgpuTextureRelease(r->texture);
      r->texture = nullptr;
    }
    params->SetValue(base::String("errMsg"), lepus::Value(error));
    params->SetValue(base::String("error_code"), lepus::Value(-1));
    event_("error", r->node, lepus::Value(std::move(params)));
  } else if (r->image) {
    ++r->uploads;
    if (!r->loaded) {
      r->loaded = true;
      params->SetValue(base::String("width"), lepus::Value(r->width));
      params->SetValue(base::String("height"), lepus::Value(r->height));
      event_("load", r->node, lepus::Value(std::move(params)));
    }
  }
  invalidate_();
}
void BrowserImages::PlaybackEvent(int token, const std::string& name) {
  auto* r = Find(token);
  if (!r || r->failed) return;
  if (name == "currentloopcomplete") ++r->loops;
  if (name == "startplay" || name == "currentloopcomplete" ||
      name == "finalloopcomplete")
    event_(name, r->node, lepus::Value(lepus::Dictionary::Create()));
}
std::vector<std::shared_ptr<skity::Image>> BrowserImages::Retain() const {
  std::vector<std::shared_ptr<skity::Image>> images;
  for (const auto& [id, r] : records_)
    if (r->image) images.push_back(r->image);
  return images;
}
void BrowserImages::Paint(skity::Canvas* canvas, tasm::ImageElement& node,
                          float x, float y, float width, float height) {
  auto it = records_.find(node.impl_id());
  if (it == records_.end() || !it->second->loaded || !it->second->image ||
      width <= 0 || height <= 0)
    return;
  const auto& r = *it->second;
  float sx = 0, sy = 0, sw = r.width, sh = r.height;
  float dx = x, dy = y, dw = width, dh = height;
  using Mode = tasm::ImageFitMode;
  if (node.paint_info().mode == Mode::kAspectFit) {
    float scale = std::min(width / sw, height / sh);
    dw = sw * scale;
    dh = sh * scale;
    dx += (width - dw) / 2;
    dy += (height - dh) / 2;
  } else if (node.paint_info().mode == Mode::kAspectFill) {
    float scale = std::max(width / sw, height / sh);
    sw = width / scale;
    sh = height / scale;
    sx = (r.width - sw) / 2;
    sy = (r.height - sh) / 2;
  } else if (node.paint_info().mode == Mode::kCenter) {
    sw = std::min(sw, width);
    sh = std::min(sh, height);
    sx = (r.width - sw) / 2;
    sy = (r.height - sh) / 2;
    dw = sw;
    dh = sh;
    dx += (width - dw) / 2;
    dy += (height - dh) / 2;
  }
  canvas->DrawImageRect(r.image, skity::Rect::MakeXYWH(sx, sy, sw, sh),
                        skity::Rect::MakeXYWH(dx, dy, dw, dh),
                        {skity::FilterMode::kLinear, skity::MipmapMode::kNone});
}
}  // namespace lynx::wasm
