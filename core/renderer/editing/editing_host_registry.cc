// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/editing/editing_host_registry.h"

#include <algorithm>

namespace lynx::editing {

bool EditingHostRegistry::Attach(
    int64_t host_id, std::shared_ptr<EditingHostController> controller) {
  if (!controller || hosts_.count(host_id) != 0 || FindHost(controller)) {
    return false;
  }
  hosts_.emplace(host_id, std::move(controller));
  NotifyLifecycle(host_id, EditingHostLifecycleEvent::kAttached);
  return true;
}

void EditingHostRegistry::Detach(int64_t host_id) {
  if (hosts_.count(host_id) == 0) {
    return;
  }
  if (active_host_id_ == host_id) {
    Deactivate(host_id);
  }
  NotifyLifecycle(host_id, EditingHostLifecycleEvent::kDetached);
  hosts_.erase(host_id);
}

bool EditingHostRegistry::Activate(int64_t host_id) {
  if (hosts_.count(host_id) == 0) {
    return false;
  }
  if (active_host_id_ == host_id) {
    return true;
  }
  if (active_host_id_) {
    const int64_t previous = *active_host_id_;
    active_host_id_.reset();
    NotifyLifecycle(previous, EditingHostLifecycleEvent::kDeactivated);
  }
  active_host_id_ = host_id;
  NotifyLifecycle(host_id, EditingHostLifecycleEvent::kActivated);
  return true;
}

void EditingHostRegistry::Deactivate(int64_t host_id) {
  if (active_host_id_ != host_id) {
    return;
  }
  active_host_id_.reset();
  NotifyLifecycle(host_id, EditingHostLifecycleEvent::kDeactivated);
}

std::shared_ptr<EditingHostController> EditingHostRegistry::Lookup(
    int64_t host_id) const {
  auto it = hosts_.find(host_id);
  return it == hosts_.end() ? nullptr : it->second;
}

std::optional<int64_t> EditingHostRegistry::FindHost(
    const std::shared_ptr<EditingHostController>& controller) const {
  for (const auto& [host_id, candidate] : hosts_) {
    if (candidate == controller) {
      return host_id;
    }
  }
  return std::nullopt;
}

std::vector<int64_t> EditingHostRegistry::host_ids() const {
  std::vector<int64_t> result;
  result.reserve(hosts_.size());
  for (const auto& [host_id, unused] : hosts_) {
    result.push_back(host_id);
  }
  std::sort(result.begin(), result.end());
  return result;
}

uint64_t EditingHostRegistry::AddLifecycleObserver(LifecycleObserver observer) {
  const uint64_t observer_id = next_observer_id_++;
  observers_.emplace(observer_id, std::move(observer));
  return observer_id;
}

void EditingHostRegistry::RemoveLifecycleObserver(uint64_t observer_id) {
  observers_.erase(observer_id);
}

void EditingHostRegistry::NotifyLifecycle(
    int64_t host_id, EditingHostLifecycleEvent event) const {
  for (const auto& [unused, observer] : observers_) {
    observer(host_id, event);
  }
}

}  // namespace lynx::editing
