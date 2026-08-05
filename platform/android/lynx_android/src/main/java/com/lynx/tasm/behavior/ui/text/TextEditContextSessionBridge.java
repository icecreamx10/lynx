// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

import java.lang.ref.WeakReference;

final class TextEditContextSessionBridge implements TextEditContextSession {
  private WeakReference<AndroidText> mHostView = new WeakReference<>(null);
  private long mNativeSessionPtr;

  private native boolean nativeActivate(long nativeSessionPtr);

  private native TextEditContextResult nativeApplyTransaction(long nativeSessionPtr,
      String inputType, boolean updatesText, int rangeStart, int rangeEnd, String replacement,
      int selectionBase, int selectionExtent, int compositionStart, int compositionEnd,
      long expectedRevision);

  private native void nativeBind(long nativeSessionPtr);

  private native void nativeDeactivate(long nativeSessionPtr);

  private native void nativeDestroy(long nativeSessionPtr);

  private native TextEditContextLayoutSnapshot nativeGetProjectionSnapshot(long nativeSessionPtr);

  private native float[] nativeGetSelectionRects(
      long nativeSessionPtr, int selectionBase, int selectionExtent, long expectedRevision);

  private native TextEditContextSnapshot nativeGetSnapshot(long nativeSessionPtr);

  private native boolean nativeIsActive(long nativeSessionPtr);

  private native TextEditContextResult nativePerformInput(
      long nativeSessionPtr, String inputType, String data, long expectedRevision);

  private native TextEditContextResult nativeSetSelectionFromPoint(
      long nativeSessionPtr, float x, float y, int anchor, long expectedRevision);

  private native boolean nativeUpdateGeometry(long nativeSessionPtr, long stateRevision,
      long projectionRevision, int projectionLength, int coverageStart, int coverageEnd,
      float[] controlBounds, int[] projectionOffsets, long[] segmentIds, long[] ownerIds,
      int[] localStarts, int[] localEnds, float[] bounds, int[] flags, int[] boundaryEdges);

  private TextEditContextSessionBridge(long nativeSessionPtr) {
    this.mNativeSessionPtr = nativeSessionPtr;
    nativeBind(nativeSessionPtr);
  }

  static TextEditContextSessionBridge create(long nativeSessionPtr) {
    return new TextEditContextSessionBridge(nativeSessionPtr);
  }

  static TextEditContextSnapshot createSnapshot(String text, int selectionBase, int selectionExtent,
      int compositionStart, int compositionEnd, long revision) {
    return new TextEditContextSnapshot(
        text, selectionBase, selectionExtent, compositionStart, compositionEnd, revision);
  }

  static TextEditContextResult createResult(
      int status, boolean restartInput, TextEditContextSnapshot snapshot) {
    return new TextEditContextResult(status, restartInput, snapshot);
  }

  static TextEditContextLayoutSnapshot createLayoutSnapshot(long stateRevision,
      long projectionRevision, int length, long[] segmentIds, long[] ownerIds, int[] kinds,
      int[] starts, int[] ends, int[] boundaryEdges, String[] texts) {
    return new TextEditContextLayoutSnapshot(stateRevision, projectionRevision, length, segmentIds,
        ownerIds, kinds, starts, ends, boundaryEdges, texts);
  }

  static void attach(AndroidText view, TextEditContextSessionBridge session) {
    if (view != null) {
      if (session != null) {
        session.mHostView = new WeakReference<>(view);
      }
      view.setTextEditContextSession(session);
    }
  }

  static void stateChanged(
      final TextEditContextSessionBridge session, final TextEditContextSnapshot snapshot) {
    AndroidText view;
    if (session != null && (view = session.mHostView.get()) != null) {
      view.post(() -> view.onTextEditContextStateChanged(session, snapshot));
    }
  }

  static void activate(AndroidText view) {
    if (view != null) {
      view.post(view::activateTextEditContext);
    }
  }

  static void deactivate(AndroidText view) {
    if (view != null) {
      view.post(view::deactivateTextEditContext);
    }
  }

  static void activationChanged(final TextEditContextSessionBridge session, final boolean active) {
    AndroidText view;
    if (session != null && (view = session.mHostView.get()) != null) {
      final AndroidText hostView = view;
      view.post(() -> {
        if (!hostView.isTextEditContextSession(session)) {
          return;
        }
        if (active) {
          hostView.activateTextEditContext();
        } else {
          hostView.deactivateTextEditContext();
        }
      });
    }
  }

  static void geometryRequested(final TextEditContextSessionBridge session, int rangeStart,
      int rangeEnd, long stateRevision, long projectionRevision) {
    AndroidText view;
    if (session != null && (view = session.mHostView.get()) != null) {
      view.post(view::refreshTextEditContextGeometry);
    }
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public TextEditContextSnapshot snapshot() {
    long nativePtr = this.mNativeSessionPtr;
    if (nativePtr == 0) {
      return null;
    }
    return nativeGetSnapshot(nativePtr);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public TextEditContextResult apply(TextEditContextSession.Transaction transaction) {
    long nativePtr = this.mNativeSessionPtr;
    if (nativePtr == 0) {
      return new TextEditContextResult(TextEditContextResult.STATUS_INACTIVE, false, null);
    }
    return nativeApplyTransaction(nativePtr, transaction.inputType, transaction.updatesText,
        transaction.rangeStart, transaction.rangeEnd, transaction.replacement,
        transaction.selectionBase, transaction.selectionExtent, transaction.compositionStart,
        transaction.compositionEnd, transaction.expectedRevision);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public TextEditContextResult performInput(String inputType, String data, long expectedRevision) {
    long nativePtr = this.mNativeSessionPtr;
    if (nativePtr == 0) {
      return new TextEditContextResult(TextEditContextResult.STATUS_INACTIVE, false, null);
    }
    return nativePerformInput(nativePtr, inputType, data, expectedRevision);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public boolean activate() {
    long nativePtr = this.mNativeSessionPtr;
    return nativePtr != 0 && nativeActivate(nativePtr);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public void deactivate() {
    long nativePtr = this.mNativeSessionPtr;
    if (nativePtr != 0) {
      nativeDeactivate(nativePtr);
    }
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public boolean isActive() {
    long nativePtr = this.mNativeSessionPtr;
    return nativePtr != 0 && nativeIsActive(nativePtr);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public boolean refreshLayout(AndroidText hostView) {
    TextEditContextLayoutSnapshot projection;
    TextEditContextLayoutData layout;
    long nativePtr = this.mNativeSessionPtr;
    return (nativePtr == 0 || hostView == null
               || (projection = nativeGetProjectionSnapshot(nativePtr)) == null
               || (layout = TextEditContextLayoutCollector.collect(hostView, projection)) == null
               || !nativeUpdateGeometry(nativePtr, layout.stateRevision, layout.projectionRevision,
                   layout.projectionLength, layout.coverageStart, layout.coverageEnd,
                   layout.controlBounds, layout.projectionOffsets, layout.segmentIds,
                   layout.ownerIds, layout.localStarts, layout.localEnds, layout.bounds,
                   layout.flags, layout.boundaryEdges))
        ? false
        : true;
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public float[] selectionRects(int selectionBase, int selectionExtent) {
    long nativePtr = this.mNativeSessionPtr;
    TextEditContextSnapshot state = snapshot();
    if (nativePtr == 0 || state == null) {
      return new float[0];
    }
    return nativeGetSelectionRects(nativePtr, selectionBase, selectionExtent, state.revision);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public TextEditContextResult setSelectionFromPoint(
      AndroidText hostView, float x, float y, int anchor, long expectedRevision) {
    if (!refreshLayout(hostView)) {
      return new TextEditContextResult(
          TextEditContextResult.STATUS_GEOMETRY_UNAVAILABLE, false, snapshot());
    }
    int[] screen = new int[2];
    hostView.getLocationInWindow(screen);
    return nativeSetSelectionFromPoint(
        this.mNativeSessionPtr, screen[0] + x, screen[1] + y, anchor, expectedRevision);
  }

  void invalidate() {
    long nativePtr = this.mNativeSessionPtr;
    if (nativePtr != 0) {
      nativeDestroy(nativePtr);
      this.mNativeSessionPtr = 0L;
    }
    this.mHostView.clear();
  }
}
