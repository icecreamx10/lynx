// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

#include <atomic>
#include <memory>
#include <string>

#include "devtool/base_devtool/native/devtool_global_slot.h"
#include "devtool/base_devtool/native/devtool_slot.h"

namespace lynx::devtool {
namespace {
struct Message {
  int view;
  std::string type, payload;
};
void Send(int view, const std::string& type, const std::string& payload) {
  auto* message = new Message{view, type, payload};
  auto deliver = [](void* data) {
    std::unique_ptr<Message> message(static_cast<Message*>(data));
    EM_ASM(
        { Module.onLynxCDP ?.($0, UTF8ToString($1), UTF8ToString($2)); },
        message->view, message->type.c_str(), message->payload.c_str());
  };
  if (emscripten_is_main_runtime_thread()) {
    deliver(message);
  } else if (!emscripten_proxy_async(emscripten_proxy_get_system_queue(),
                                     emscripten_main_runtime_thread_id(),
                                     deliver, message)) {
    delete message;
  }
}
class BrowserSlot final : public DevToolSlot {
 public:
  using DevToolSlot::DevToolSlot;
  int32_t Plug(const std::string&) override {
    static std::atomic<int32_t> next{1};
    view_ = next++;
    return view_;
  }
  void Pull() override { view_ = 0; }
  void SendMessage(const std::string& type, const std::string& msg) override {
    const int view = view_;
    if (view) Send(view, type, msg);
  }

 private:
  std::atomic<int32_t> view_{0};
};
class BrowserGlobalSlot final : public DevToolGlobalSlot {
 public:
  using DevToolGlobalSlot::DevToolGlobalSlot;
  void SendMessage(const std::string& type, const std::string& msg) override {
    Send(0, type, msg);
  }
};
}  // namespace
std::shared_ptr<DevToolSlot> DevToolSlot::Create(
    const std::shared_ptr<DebugRouterMessageSubscriber>& delegate) {
  return std::make_shared<BrowserSlot>(delegate);
}
std::shared_ptr<DevToolGlobalSlot> DevToolGlobalSlot::Create(
    const std::shared_ptr<DebugRouterMessageSubscriber>& delegate) {
  return std::make_shared<BrowserGlobalSlot>(delegate);
}
}  // namespace lynx::devtool
