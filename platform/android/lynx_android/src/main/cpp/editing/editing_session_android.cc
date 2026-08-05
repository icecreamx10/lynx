// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "platform/android/lynx_android/src/main/cpp/editing/editing_session_android.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/base/android/android_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/TextEditContextSessionBridge_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/TextEditContextSessionBridge_register_jni.h"

using lynx::editing::EditContextRect;
using lynx::editing::EditingBlockBoundaryEdge;
using lynx::editing::EditingGeometrySnapshot;
using lynx::editing::EditingLayoutPoint;
using lynx::editing::EditingLayoutUnit;
using lynx::editing::EditingLayoutUnitFlag;
using lynx::editing::EditingOperationStatus;
using lynx::editing::EditingPlatformDelegate;
using lynx::editing::EditingPlatformResult;
using lynx::editing::EditingPlatformSession;
using lynx::editing::EditingProjectionSnapshot;
using lynx::editing::EditingStateSnapshot;
using lynx::editing::EditingStateUpdate;
using lynx::editing::NativeTextTransaction;
using lynx::editing::TextRange;

constexpr jint kNoComposition = -1;
constexpr jint kNoAnchor = -1;
static_assert(static_cast<jint>(EditingOperationStatus::kAccepted) == 0);
static_assert(static_cast<jint>(EditingOperationStatus::kInactive) == 1);
static_assert(static_cast<jint>(EditingOperationStatus::kStaleRevision) == 2);
static_assert(static_cast<jint>(EditingOperationStatus::kInvalidRange) == 3);
static_assert(
    static_cast<jint>(EditingOperationStatus::kUnsupportedInputType) == 4);
static_assert(static_cast<jint>(EditingOperationStatus::kGeometryUnavailable) ==
              5);

std::u16string FromJavaString(JNIEnv* env, jstring value) {
  if (!value) {
    return {};
  }
  const jsize length = env->GetStringLength(value);
  const jchar* chars = env->GetStringChars(value, nullptr);
  if (!chars) {
    return {};
  }
  std::u16string result(reinterpret_cast<const char16_t*>(chars), length);
  env->ReleaseStringChars(value, chars);
  return result;
}

std::string FromJavaUtf8String(JNIEnv* env, jstring value) {
  if (!value) {
    return {};
  }
  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (!chars) {
    return {};
  }
  std::string result(chars);
  env->ReleaseStringUTFChars(value, chars);
  return result;
}

jstring ToJavaString(JNIEnv* env, const std::u16string& value) {
  return env->NewString(reinterpret_cast<const jchar*>(value.data()),
                        static_cast<jsize>(value.size()));
}

bool FitsJavaInt(size_t value) {
  return value <= static_cast<size_t>(std::numeric_limits<jint>::max());
}

class EditingSessionAndroid final : public EditingPlatformDelegate {
 public:
  explicit EditingSessionAndroid(
      std::shared_ptr<EditingPlatformSession> session)
      : session_(std::move(session)) {}

  ~EditingSessionAndroid() override {
    if (session_) {
      session_->SetDelegate(nullptr);
      session_->Detach();
    }
    if (bridge_) {
      lynx::base::android::AttachCurrentThread()->DeleteWeakGlobalRef(bridge_);
    }
  }

  EditingPlatformSession* session() const { return session_.get(); }

  void Bind(JNIEnv* env, jobject bridge) {
    if (bridge_) {
      env->DeleteWeakGlobalRef(bridge_);
    }
    bridge_ = env->NewWeakGlobalRef(bridge);
    session_->SetDelegate(this);
  }

  jobject NewSnapshot(JNIEnv* env, const EditingStateSnapshot& snapshot) const {
    if (!FitsJavaInt(snapshot.text.size()) ||
        !FitsJavaInt(snapshot.selection.base()) ||
        !FitsJavaInt(snapshot.selection.extent()) ||
        (snapshot.has_composition &&
         (!FitsJavaInt(snapshot.composition.start()) ||
          !FitsJavaInt(snapshot.composition.end())))) {
      return nullptr;
    }
    jmethodID method = env->GetStaticMethodID(
        TextEditContextSessionBridge_clazz(env), "createSnapshot",
        "(Ljava/lang/String;IIIIJ)Lcom/lynx/tasm/behavior/ui/text/"
        "TextEditContextSnapshot;");
    jstring text = ToJavaString(env, snapshot.text);
    jobject result = env->CallStaticObjectMethod(
        TextEditContextSessionBridge_clazz(env), method, text,
        static_cast<jint>(snapshot.selection.base()),
        static_cast<jint>(snapshot.selection.extent()),
        snapshot.has_composition
            ? static_cast<jint>(snapshot.composition.start())
            : kNoComposition,
        snapshot.has_composition ? static_cast<jint>(snapshot.composition.end())
                                 : kNoComposition,
        static_cast<jlong>(snapshot.revision));
    env->DeleteLocalRef(text);
    return result;
  }

  jobject NewResult(JNIEnv* env, const EditingPlatformResult& result) const {
    jobject snapshot = NewSnapshot(env, result.snapshot);
    if (!snapshot) {
      return nullptr;
    }
    jmethodID method = env->GetStaticMethodID(
        TextEditContextSessionBridge_clazz(env), "createResult",
        "(IZLcom/lynx/tasm/behavior/ui/text/TextEditContextSnapshot;)Lcom/lynx/"
        "tasm/behavior/ui/text/TextEditContextResult;");
    jobject java_result = env->CallStaticObjectMethod(
        TextEditContextSessionBridge_clazz(env), method,
        static_cast<jint>(result.status),
        static_cast<jboolean>(result.restart_input), snapshot);
    env->DeleteLocalRef(snapshot);
    return java_result;
  }

  jobject NewProjectionSnapshot(JNIEnv* env) const {
    EditingProjectionSnapshot projection = session_->ProjectionSnapshot();
    EditingStateSnapshot state = session_->Snapshot();
    if (!FitsJavaInt(projection.length) ||
        !FitsJavaInt(projection.segments.size())) {
      return nullptr;
    }
    const jsize count = static_cast<jsize>(projection.segments.size());
    jlongArray segment_ids = env->NewLongArray(count);
    jlongArray owner_ids = env->NewLongArray(count);
    jintArray kinds = env->NewIntArray(count);
    jintArray starts = env->NewIntArray(count);
    jintArray ends = env->NewIntArray(count);
    jintArray boundary_edges = env->NewIntArray(count);
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray texts = env->NewObjectArray(count, string_class, nullptr);
    std::vector<jlong> segment_id_values(count);
    std::vector<jlong> owner_id_values(count);
    std::vector<jint> kind_values(count);
    std::vector<jint> start_values(count);
    std::vector<jint> end_values(count);
    std::vector<jint> edge_values(count);
    for (jsize index = 0; index < count; ++index) {
      const auto& segment = projection.segments[index];
      if (!FitsJavaInt(segment.start) || !FitsJavaInt(segment.end)) {
        return nullptr;
      }
      segment_id_values[index] = segment.segment_id;
      owner_id_values[index] = segment.owner_id;
      kind_values[index] = static_cast<jint>(segment.kind);
      start_values[index] = static_cast<jint>(segment.start);
      end_values[index] = static_cast<jint>(segment.end);
      edge_values[index] = static_cast<jint>(segment.boundary_edge);
      jstring text = ToJavaString(env, segment.text);
      env->SetObjectArrayElement(texts, index, text);
      env->DeleteLocalRef(text);
    }
    env->SetLongArrayRegion(segment_ids, 0, count, segment_id_values.data());
    env->SetLongArrayRegion(owner_ids, 0, count, owner_id_values.data());
    env->SetIntArrayRegion(kinds, 0, count, kind_values.data());
    env->SetIntArrayRegion(starts, 0, count, start_values.data());
    env->SetIntArrayRegion(ends, 0, count, end_values.data());
    env->SetIntArrayRegion(boundary_edges, 0, count, edge_values.data());
    jmethodID method = env->GetStaticMethodID(
        TextEditContextSessionBridge_clazz(env), "createLayoutSnapshot",
        "(JJI[J[J[I[I[I[I[Ljava/lang/String;)Lcom/lynx/tasm/behavior/ui/text/"
        "TextEditContextLayoutSnapshot;");
    return env->CallStaticObjectMethod(
        TextEditContextSessionBridge_clazz(env), method,
        static_cast<jlong>(state.revision),
        static_cast<jlong>(projection.revision),
        static_cast<jint>(projection.length), segment_ids, owner_ids, kinds,
        starts, ends, boundary_edges, texts);
  }

  void OnStateChanged(const EditingStateUpdate& update) override {
    JNIEnv* env = lynx::base::android::AttachCurrentThread();
    jobject bridge = env->NewLocalRef(bridge_);
    if (!bridge) {
      return;
    }
    jobject snapshot = NewSnapshot(env, update.snapshot);
    jmethodID method = env->GetStaticMethodID(
        TextEditContextSessionBridge_clazz(env), "stateChanged",
        "(Lcom/lynx/tasm/behavior/ui/text/TextEditContextSessionBridge;Lcom/"
        "lynx/"
        "tasm/behavior/ui/text/TextEditContextSnapshot;)V");
    env->CallStaticVoidMethod(TextEditContextSessionBridge_clazz(env), method,
                              bridge, snapshot);
    env->DeleteLocalRef(snapshot);
    env->DeleteLocalRef(bridge);
  }

  void OnActivationChanged(bool active) override {
    JNIEnv* env = lynx::base::android::AttachCurrentThread();
    jobject bridge = env->NewLocalRef(bridge_);
    if (!bridge) {
      return;
    }
    jmethodID method = env->GetStaticMethodID(
        TextEditContextSessionBridge_clazz(env), "activationChanged",
        "(Lcom/lynx/tasm/behavior/ui/text/TextEditContextSessionBridge;Z)V");
    env->CallStaticVoidMethod(TextEditContextSessionBridge_clazz(env), method,
                              bridge, static_cast<jboolean>(active));
    env->DeleteLocalRef(bridge);
  }

  void OnGeometryRequested(const TextRange& range, uint64_t state_revision,
                           uint64_t projection_revision) override {
    if (!FitsJavaInt(range.start()) || !FitsJavaInt(range.end())) {
      return;
    }
    JNIEnv* env = lynx::base::android::AttachCurrentThread();
    jobject bridge = env->NewLocalRef(bridge_);
    if (!bridge) {
      return;
    }
    jmethodID method = env->GetStaticMethodID(
        TextEditContextSessionBridge_clazz(env), "geometryRequested",
        "(Lcom/lynx/tasm/behavior/ui/text/TextEditContextSessionBridge;IIJJ)V");
    env->CallStaticVoidMethod(TextEditContextSessionBridge_clazz(env), method,
                              bridge, static_cast<jint>(range.start()),
                              static_cast<jint>(range.end()),
                              static_cast<jlong>(state_revision),
                              static_cast<jlong>(projection_revision));
    env->DeleteLocalRef(bridge);
  }

 private:
  std::shared_ptr<EditingPlatformSession> session_;
  jweak bridge_{nullptr};
};

EditingSessionAndroid* FromPtr(jlong native_session_ptr) {
  return reinterpret_cast<EditingSessionAndroid*>(native_session_ptr);
}

jboolean Activate(JNIEnv*, jobject, jlong native_session_ptr) {
  auto* adapter = FromPtr(native_session_ptr);
  return adapter && adapter->session()->Activate();
}

jobject ApplyTransaction(JNIEnv* env, jobject, jlong native_session_ptr,
                         jstring input_type, jboolean updates_text,
                         jint range_start, jint range_end, jstring replacement,
                         jint selection_base, jint selection_extent,
                         jint composition_start, jint composition_end,
                         jlong expected_revision) {
  auto* adapter = FromPtr(native_session_ptr);
  if (!adapter || range_start < 0 || range_end < 0 || selection_base < 0 ||
      selection_extent < 0 || composition_start < kNoComposition ||
      composition_end < kNoComposition) {
    return nullptr;
  }
  std::optional<TextRange> composition;
  if (composition_start != kNoComposition ||
      composition_end != kNoComposition) {
    if (composition_start < 0 || composition_end < 0) {
      return nullptr;
    }
    composition.emplace(static_cast<size_t>(composition_start),
                        static_cast<size_t>(composition_end));
  }
  NativeTextTransaction transaction{
      FromJavaUtf8String(env, input_type),
      static_cast<bool>(updates_text),
      TextRange(static_cast<size_t>(range_start),
                static_cast<size_t>(range_end)),
      FromJavaString(env, replacement),
      TextRange(static_cast<size_t>(selection_base),
                static_cast<size_t>(selection_extent)),
      composition,
      static_cast<uint64_t>(expected_revision)};
  return adapter->NewResult(env,
                            adapter->session()->ApplyTransaction(transaction));
}

void Bind(JNIEnv* env, jobject caller, jlong native_session_ptr) {
  auto* adapter = FromPtr(native_session_ptr);
  if (adapter) {
    adapter->Bind(env, caller);
  }
}

void Deactivate(JNIEnv*, jobject, jlong native_session_ptr) {
  auto* adapter = FromPtr(native_session_ptr);
  if (adapter) {
    adapter->session()->Deactivate();
  }
}

void Destroy(JNIEnv*, jobject, jlong native_session_ptr) {
  delete FromPtr(native_session_ptr);
}

jobject GetProjectionSnapshot(JNIEnv* env, jobject, jlong native_session_ptr) {
  auto* adapter = FromPtr(native_session_ptr);
  return adapter ? adapter->NewProjectionSnapshot(env) : nullptr;
}

jfloatArray GetSelectionRects(JNIEnv* env, jobject, jlong native_session_ptr,
                              jint selection_base, jint selection_extent,
                              jlong expected_revision) {
  auto* adapter = FromPtr(native_session_ptr);
  if (!adapter || selection_base < 0 || selection_extent < 0) {
    return env->NewFloatArray(0);
  }
  auto query = adapter->session()->QuerySelectionRects(
      TextRange(static_cast<size_t>(selection_base),
                static_cast<size_t>(selection_extent)),
      static_cast<uint64_t>(expected_revision));
  auto& rects = query.rects;
  if (rects.size() >
      static_cast<size_t>(std::numeric_limits<jsize>::max()) / 4) {
    return env->NewFloatArray(0);
  }
  jfloatArray result = env->NewFloatArray(static_cast<jsize>(rects.size() * 4));
  std::vector<jfloat> values;
  values.reserve(rects.size() * 4);
  for (const EditContextRect& rect : rects) {
    values.insert(values.end(), {rect.x, rect.y, rect.width, rect.height});
  }
  env->SetFloatArrayRegion(result, 0, static_cast<jsize>(values.size()),
                           values.data());
  return result;
}

jobject GetSnapshot(JNIEnv* env, jobject, jlong native_session_ptr) {
  auto* adapter = FromPtr(native_session_ptr);
  return adapter ? adapter->NewSnapshot(env, adapter->session()->Snapshot())
                 : nullptr;
}

jboolean IsActive(JNIEnv*, jobject, jlong native_session_ptr) {
  auto* adapter = FromPtr(native_session_ptr);
  return adapter && adapter->session()->IsActive();
}

jobject PerformInput(JNIEnv* env, jobject, jlong native_session_ptr,
                     jstring input_type, jstring data,
                     jlong expected_revision) {
  auto* adapter = FromPtr(native_session_ptr);
  if (!adapter) {
    return nullptr;
  }
  return adapter->NewResult(
      env, adapter->session()->PerformInput(
               FromJavaUtf8String(env, input_type), FromJavaString(env, data),
               static_cast<uint64_t>(expected_revision)));
}

jobject SetSelectionFromPoint(JNIEnv* env, jobject, jlong native_session_ptr,
                              jfloat x, jfloat y, jint anchor,
                              jlong expected_revision) {
  auto* adapter = FromPtr(native_session_ptr);
  if (!adapter || anchor < kNoAnchor) {
    return nullptr;
  }
  std::optional<size_t> native_anchor =
      anchor == kNoAnchor ? std::nullopt
                          : std::optional<size_t>(static_cast<size_t>(anchor));
  return adapter->NewResult(env, adapter->session()->SetSelectionFromPoint(
                                     EditingLayoutPoint{x, y}, native_anchor,
                                     static_cast<uint64_t>(expected_revision)));
}

jboolean UpdateGeometry(JNIEnv* env, jobject, jlong native_session_ptr,
                        jlong state_revision, jlong projection_revision,
                        jint projection_length, jint coverage_start,
                        jint coverage_end, jfloatArray control_bounds,
                        jintArray projection_offsets, jlongArray segment_ids,
                        jlongArray owner_ids, jintArray local_starts,
                        jintArray local_ends, jfloatArray bounds,
                        jintArray flags, jintArray boundary_edges) {
  auto* adapter = FromPtr(native_session_ptr);
  if (!adapter || projection_length < 0 || coverage_start < 0 ||
      coverage_end < coverage_start || coverage_end > projection_length ||
      !control_bounds || env->GetArrayLength(control_bounds) != 4 ||
      !projection_offsets || !segment_ids || !owner_ids || !local_starts ||
      !local_ends || !bounds || !flags || !boundary_edges) {
    return false;
  }
  const jsize count = env->GetArrayLength(projection_offsets);
  if (count > std::numeric_limits<jsize>::max() / 4) {
    return false;
  }
  if (env->GetArrayLength(segment_ids) != count ||
      env->GetArrayLength(owner_ids) != count ||
      env->GetArrayLength(local_starts) != count ||
      env->GetArrayLength(local_ends) != count ||
      env->GetArrayLength(bounds) != count * 4 ||
      env->GetArrayLength(flags) != count ||
      env->GetArrayLength(boundary_edges) != count) {
    return false;
  }
  std::vector<jfloat> control_values(4);
  std::vector<jint> offset_values(count), start_values(count),
      end_values(count), flag_values(count), edge_values(count);
  std::vector<jlong> segment_values(count), owner_values(count);
  std::vector<jfloat> bound_values(count * 4);
  env->GetFloatArrayRegion(control_bounds, 0, 4, control_values.data());
  env->GetIntArrayRegion(projection_offsets, 0, count, offset_values.data());
  env->GetLongArrayRegion(segment_ids, 0, count, segment_values.data());
  env->GetLongArrayRegion(owner_ids, 0, count, owner_values.data());
  env->GetIntArrayRegion(local_starts, 0, count, start_values.data());
  env->GetIntArrayRegion(local_ends, 0, count, end_values.data());
  env->GetFloatArrayRegion(bounds, 0, count * 4, bound_values.data());
  env->GetIntArrayRegion(flags, 0, count, flag_values.data());
  env->GetIntArrayRegion(boundary_edges, 0, count, edge_values.data());
  EditingGeometrySnapshot geometry;
  geometry.state_revision = static_cast<uint64_t>(state_revision);
  geometry.projection_revision = static_cast<uint64_t>(projection_revision);
  geometry.projection_length = static_cast<size_t>(projection_length);
  geometry.coverage = TextRange(static_cast<size_t>(coverage_start),
                                static_cast<size_t>(coverage_end));
  geometry.control_bounds = {control_values[0], control_values[1],
                             control_values[2], control_values[3]};
  geometry.units.reserve(count);
  for (jsize index = 0; index < count; ++index) {
    if (offset_values[index] < 0 || start_values[index] < 0 ||
        end_values[index] < 0) {
      return false;
    }
    const jsize rect_index = index * 4;
    geometry.units.push_back(
        {static_cast<size_t>(offset_values[index]),
         segment_values[index],
         owner_values[index],
         static_cast<size_t>(start_values[index]),
         static_cast<size_t>(end_values[index]),
         {bound_values[rect_index], bound_values[rect_index + 1],
          bound_values[rect_index + 2], bound_values[rect_index + 3]},
         static_cast<EditingLayoutUnitFlag>(flag_values[index]),
         static_cast<EditingBlockBoundaryEdge>(edge_values[index])});
  }
  return adapter->session()->UpdateGeometry(std::move(geometry)) ==
         EditingOperationStatus::kAccepted;
}

namespace lynx::jni {

bool RegisterJNIForTextEditContextSessionBridge(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace lynx::jni

namespace lynx::editing {

bool AttachEditingSessionAndroid(
    JNIEnv* env, jobject android_text,
    std::shared_ptr<EditingPlatformSession> session) {
  if (!env || !android_text || !session) {
    return false;
  }
  session->Attach();
  auto* adapter = new EditingSessionAndroid(std::move(session));
  jmethodID create = env->GetStaticMethodID(
      TextEditContextSessionBridge_clazz(env), "create",
      "(J)Lcom/lynx/tasm/behavior/ui/text/TextEditContextSessionBridge;");
  jobject bridge =
      env->CallStaticObjectMethod(TextEditContextSessionBridge_clazz(env),
                                  create, reinterpret_cast<jlong>(adapter));
  if (!bridge || env->ExceptionCheck()) {
    delete adapter;
    return false;
  }
  jmethodID attach = env->GetStaticMethodID(
      TextEditContextSessionBridge_clazz(env), "attach",
      "(Lcom/lynx/tasm/behavior/ui/text/AndroidText;Lcom/lynx/tasm/behavior/ui/"
      "text/TextEditContextSessionBridge;)V");
  env->CallStaticVoidMethod(TextEditContextSessionBridge_clazz(env), attach,
                            android_text, bridge);
  env->DeleteLocalRef(bridge);
  return !env->ExceptionCheck();
}

void ActivateEditingSessionAndroid(JNIEnv* env, jobject android_text) {
  if (!env || !android_text) {
    return;
  }
  jmethodID activate = env->GetStaticMethodID(
      TextEditContextSessionBridge_clazz(env), "activate",
      "(Lcom/lynx/tasm/behavior/ui/text/AndroidText;)V");
  env->CallStaticVoidMethod(TextEditContextSessionBridge_clazz(env), activate,
                            android_text);
}

void DeactivateEditingSessionAndroid(JNIEnv* env, jobject android_text) {
  if (!env || !android_text) {
    return;
  }
  jmethodID deactivate = env->GetStaticMethodID(
      TextEditContextSessionBridge_clazz(env), "deactivate",
      "(Lcom/lynx/tasm/behavior/ui/text/AndroidText;)V");
  env->CallStaticVoidMethod(TextEditContextSessionBridge_clazz(env), deactivate,
                            android_text);
}

void DetachEditingSessionAndroid(JNIEnv* env, jobject android_text) {
  if (!env || !android_text) {
    return;
  }
  jmethodID attach = env->GetStaticMethodID(
      TextEditContextSessionBridge_clazz(env), "attach",
      "(Lcom/lynx/tasm/behavior/ui/text/AndroidText;Lcom/lynx/tasm/behavior/ui/"
      "text/TextEditContextSessionBridge;)V");
  env->CallStaticVoidMethod(TextEditContextSessionBridge_clazz(env), attach,
                            android_text, nullptr);
}

}  // namespace lynx::editing
