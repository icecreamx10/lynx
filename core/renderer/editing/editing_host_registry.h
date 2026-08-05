// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_EDITING_EDITING_HOST_REGISTRY_H_
#define CORE_RENDERER_EDITING_EDITING_HOST_REGISTRY_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/renderer/editing/editing_types.h"

namespace lynx::editing {

class EditingHostController;

class EditingHostRegistry {
 public:
  using LifecycleObserver =
      std::function<void(int64_t, EditingHostLifecycleEvent)>;

  bool Attach(int64_t host_id,
              std::shared_ptr<EditingHostController> controller);
  void Detach(int64_t host_id);
  bool Activate(int64_t host_id);
  void Deactivate(int64_t host_id);

  std::shared_ptr<EditingHostController> Lookup(int64_t host_id) const;
  std::optional<int64_t> FindHost(
      const std::shared_ptr<EditingHostController>& controller) const;
  std::vector<int64_t> host_ids() const;
  std::optional<int64_t> active_host_id() const { return active_host_id_; }

  uint64_t AddLifecycleObserver(LifecycleObserver observer);
  void RemoveLifecycleObserver(uint64_t observer_id);

 private:
  void NotifyLifecycle(int64_t host_id, EditingHostLifecycleEvent event) const;

  std::unordered_map<int64_t, std::shared_ptr<EditingHostController>> hosts_;
  std::unordered_map<uint64_t, LifecycleObserver> observers_;
  std::optional<int64_t> active_host_id_;
  uint64_t next_observer_id_{1};
};

}  // namespace lynx::editing

#endif  // CORE_RENDERER_EDITING_EDITING_HOST_REGISTRY_H_
