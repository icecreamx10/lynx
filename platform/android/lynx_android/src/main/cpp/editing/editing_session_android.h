// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef PLATFORM_ANDROID_LYNX_ANDROID_SRC_MAIN_CPP_EDITING_EDITING_SESSION_ANDROID_H_
#define PLATFORM_ANDROID_LYNX_ANDROID_SRC_MAIN_CPP_EDITING_EDITING_SESSION_ANDROID_H_

#include <jni.h>

#include <memory>

#include "core/renderer/editing/editing_platform_contract.h"

namespace lynx::editing {

// Creates the Java InputConnection-facing adapter and attaches it to
// |android_text|. The Java view owns the returned native adapter lifetime and
// releases it when its editing session is replaced or detached.
bool AttachEditingSessionAndroid(
    JNIEnv* env, jobject android_text,
    std::shared_ptr<EditingPlatformSession> session);
void ActivateEditingSessionAndroid(JNIEnv* env, jobject android_text);
void DeactivateEditingSessionAndroid(JNIEnv* env, jobject android_text);
void DetachEditingSessionAndroid(JNIEnv* env, jobject android_text);

}  // namespace lynx::editing

#endif  // PLATFORM_ANDROID_LYNX_ANDROID_SRC_MAIN_CPP_EDITING_EDITING_SESSION_ANDROID_H_
