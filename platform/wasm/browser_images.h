// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_BROWSER_IMAGES_H_
#define PLATFORM_WASM_BROWSER_IMAGES_H_
#include <functional>
#include <memory>
#include <skity/gpu/gpu_context_web.hpp>
#include <skity/graphic/image.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/renderer/dom/fiber/image_element.h"
namespace lynx::wasm {
// Image decoding stays in the browser; only dimensions and GPU handles cross
// the WASM boundary. Requests are identified independently of reusable node
// IDs.
class BrowserImages {
 public:
  using Event =
      std::function<void(const std::string&, int, const lepus::Value&)>;
  BrowserImages(skity::GPUContext* gpu, WGPUDevice device, Event event,
                std::function<void()> invalidate);
  ~BrowserImages();
  void Reset();
  void Sync(tasm::Element* root);
  void Tick(double time);
  void Paint(skity::Canvas* canvas, tasm::ImageElement& node, float x, float y,
             float width, float height);
  WGPUTexture Allocate(int token, int width, int height);
  void Complete(int token, const std::string& error);
  void PlaybackEvent(int token, const std::string& name);
  std::vector<std::shared_ptr<skity::Image>> Retain() const;
  struct Record {
    int token = 0, node = 0, width = 0, height = 0;
    int uploads = 0, loops = 0;
    bool loaded = false, failed = false, autoplay = true;
    int loop_count = 0;
    std::string src, error;
    WGPUTexture texture = nullptr;
    std::shared_ptr<skity::Image> image;
    ~Record();
  };
  const std::unordered_map<int, std::shared_ptr<Record>>& records() const {
    return records_;
  }

 private:
  Record* Find(int token);
  skity::GPUContext* gpu_;
  WGPUDevice device_;
  Event event_;
  std::function<void()> invalidate_;
  int next_token_ = 0;
  std::unordered_map<int, std::shared_ptr<Record>> records_;
};
}  // namespace lynx::wasm
#endif  // PLATFORM_WASM_BROWSER_IMAGES_H_
