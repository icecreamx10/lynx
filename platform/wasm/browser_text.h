// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_BROWSER_TEXT_H_
#define PLATFORM_WASM_BROWSER_TEXT_H_
#include <memory>

#include "core/renderer/dom/element.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
namespace skity {
class Canvas;
}
namespace lynx::wasm {
// Each page owns its paragraph cache. Fonts and the shaping engine stay on
// main.
class BrowserText {
 public:
  BrowserText();
  ~BrowserText();
  tasm::LayoutResult Measure(tasm::Element* element, float width,
                             int width_mode, float height, int height_mode);
  void Align(tasm::Element* element);
  void Paint(skity::Canvas* canvas, tasm::Element* element, float x, float y);
  void Erase(tasm::Element* element);
  void Report(tasm::Element* element,
              rapidjson::Writer<rapidjson::StringBuffer>& writer) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace lynx::wasm
#endif
