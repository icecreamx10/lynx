// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
#ifndef PLATFORM_WASM_DEVTOOL_NATIVE_H_
#define PLATFORM_WASM_DEVTOOL_NATIVE_H_
#include <functional>
#include <string>
#include <unordered_map>
#include "core/renderer/dom/element.h"
#include "json/json.h"
namespace lynx::wasm {
class BrowserDevTool {
 public:
  using Emit = std::function<void(const std::string&)>;
  BrowserDevTool(std::function<tasm::Element*()> root, std::function<void()> flush,
                 Emit emit) : root_(root), flush_(flush), emit_(emit) {}
  void Dispatch(const std::string& raw);
  void Event(const std::string& method, const Json::Value& params);
  void Reset();
  void Poll();
  int Highlighted() const { return highlight_; }
  Json::Value HighlightBox() { return Box(Find(highlight_)); }
  Json::Value Box(tasm::Element* node);
  void SetRuntime(std::function<void(const std::string&)> runtime) { runtime_ = std::move(runtime); }
 private:
  Json::Value Node(tasm::Element* node, int depth = -1);
  Json::Value Style(tasm::Element* node);
  tasm::Element* Find(int id);
  std::function<tasm::Element*()> root_;
  std::function<void()> flush_;
  Emit emit_;
  std::function<void(const std::string&)> runtime_;
  std::unordered_map<int, Json::Value> snapshot_;
  int highlight_ = 0;
  bool dom_enabled_ = false;
  bool document_sent_ = false;
};
}  // namespace lynx::wasm
#endif
