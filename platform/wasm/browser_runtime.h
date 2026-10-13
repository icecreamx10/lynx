// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#ifndef PLATFORM_WASM_BROWSER_RUNTIME_H_
#define PLATFORM_WASM_BROWSER_RUNTIME_H_
#include <map>
#include <memory>
#include <vector>

#include "core/public/vsync_observer_interface.h"
namespace lynx::tasm {
class BrowserFrameClock : public runtime::IVSyncObserver {
 public:
  using Callback = base::MoveOnlyClosure<void, int64_t, int64_t>;
  void RequestAnimationFrame(uintptr_t id, Callback cb) override {
    callbacks_[id] = std::move(cb);
  }
  void RequestBeforeAnimationFrame(uintptr_t id, Callback cb) override {
    before_[id] = std::move(cb);
  }
  void RegisterAfterAnimationFrameListener(Callback cb) override {
    after_.push_back(std::make_shared<Callback>(std::move(cb)));
  }
  void Tick(int64_t time) {
    auto before = std::move(before_);
    before_.clear();
    auto callbacks = std::move(callbacks_);
    callbacks_.clear();
    for (auto& p : before) p.second(time, time + 16666667);
    for (auto& p : callbacks) p.second(time, time + 16666667);
    auto after = after_;
    for (auto& cb : after) (*cb)(time, time + 16666667);
  }
  size_t Pending() const { return callbacks_.size() + before_.size(); }

 private:
  std::map<uintptr_t, Callback> callbacks_, before_;
  std::vector<std::shared_ptr<Callback>> after_;
};
}  // namespace lynx::tasm
#endif
