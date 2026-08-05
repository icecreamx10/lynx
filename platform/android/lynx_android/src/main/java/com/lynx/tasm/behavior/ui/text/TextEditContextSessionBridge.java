// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

import java.lang.ref.WeakReference;

final class TextEditContextSessionBridge implements TextEditContextSession {
  private final long mHostId;
  private WeakReference<AndroidText> mHostView = new WeakReference<>(null);
  private long mNativeLayoutSessionPtr;
  private long mRegistryPtr;

  private native boolean nativeActivate(long j, long j2);

  private native TextEditContextResult nativeApplyTransaction(long j, long j2, String str, int i,
      int i2, String str2, int i3, int i4, int i5, int i6, long j3);

  private native void nativeDeactivate(long j, long j2);

  private native void nativeDestroyLayoutSession(long j);

  private native TextEditContextLayoutSnapshot nativeGetLayoutSnapshot(long j);

  private native float[] nativeGetSelectionRects(long j, int i, int i2);

  private native TextEditContextSnapshot nativeGetSnapshot(long j, long j2);

  private native boolean nativeIsActive(long j, long j2);

  private native TextEditContextResult nativePerformInput(
      long j, long j2, String str, String str2, long j3);

  private native TextEditContextResult nativeSetSelectionFromPoint(
      long j, float f, float f2, int i, long j2);

  private native boolean nativeUpdateLayout(long j, long j2, float[] fArr, long[] jArr,
      long[] jArr2, int[] iArr, int[] iArr2, float[] fArr2, int[] iArr3, int[] iArr4);

  private TextEditContextSessionBridge(long nativeLayoutSessionPtr, long registryPtr, long hostId) {
    this.mNativeLayoutSessionPtr = nativeLayoutSessionPtr;
    this.mRegistryPtr = registryPtr;
    this.mHostId = hostId;
  }

  static TextEditContextSessionBridge create(
      long nativeLayoutSessionPtr, long registryPtr, long hostId) {
    return new TextEditContextSessionBridge(nativeLayoutSessionPtr, registryPtr, hostId);
  }

  static TextEditContextSnapshot createSnapshot(String text, int selectionBase, int selectionExtent,
      int compositionStart, int compositionEnd, long revision) {
    return new TextEditContextSnapshot(
        text, selectionBase, selectionExtent, compositionStart, compositionEnd, revision);
  }

  static TextEditContextResult createResult(
      boolean accepted, boolean restartInput, TextEditContextSnapshot snapshot) {
    return new TextEditContextResult(accepted, restartInput, snapshot);
  }

  static TextEditContextLayoutSnapshot createLayoutSnapshot(long revision, long[] segmentIds,
      long[] ownerIds, int[] kinds, int[] starts, int[] ends, int[] boundaryEdges, String[] texts) {
    return new TextEditContextLayoutSnapshot(
        revision, segmentIds, ownerIds, kinds, starts, ends, boundaryEdges, texts);
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
      view.activateTextEditContext();
    }
  }

  static void deactivate(AndroidText view) {
    if (view != null) {
      view.deactivateTextEditContext();
    }
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public TextEditContextSnapshot snapshot() {
    long j = this.mRegistryPtr;
    if (j == 0) {
      return null;
    }
    return nativeGetSnapshot(j, this.mHostId);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public TextEditContextResult apply(TextEditContextSession.Transaction transaction) {
    long j = this.mRegistryPtr;
    if (j == 0) {
      return new TextEditContextResult(false, false, null);
    }
    return nativeApplyTransaction(j, this.mHostId, transaction.inputType, transaction.rangeStart,
        transaction.rangeEnd, transaction.replacement, transaction.selectionBase,
        transaction.selectionExtent, transaction.compositionStart, transaction.compositionEnd,
        transaction.expectedRevision);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public TextEditContextResult performInput(String inputType, String data, long expectedRevision) {
    long j = this.mRegistryPtr;
    if (j == 0) {
      return new TextEditContextResult(false, false, null);
    }
    return nativePerformInput(j, this.mHostId, inputType, data, expectedRevision);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public boolean activate() {
    long j = this.mRegistryPtr;
    return j != 0 && nativeActivate(j, this.mHostId);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public void deactivate() {
    long j = this.mRegistryPtr;
    if (j != 0) {
      nativeDeactivate(j, this.mHostId);
    }
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public boolean isActive() {
    long j = this.mRegistryPtr;
    return j != 0 && nativeIsActive(j, this.mHostId);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public boolean refreshLayout(AndroidText hostView) {
    TextEditContextLayoutSnapshot projection;
    TextEditContextLayoutData layout;
    long j = this.mNativeLayoutSessionPtr;
    return (j == 0 || hostView == null || (projection = nativeGetLayoutSnapshot(j)) == null
               || (layout = TextEditContextLayoutCollector.collect(hostView, projection)) == null
               || !nativeUpdateLayout(this.mNativeLayoutSessionPtr, layout.revision,
                   layout.controlBounds, layout.segmentIds, layout.ownerIds, layout.localStarts,
                   layout.localEnds, layout.bounds, layout.flags, layout.boundaryEdges))
        ? false
        : true;
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public float[] selectionRects(int selectionBase, int selectionExtent) {
    long j = this.mNativeLayoutSessionPtr;
    if (j == 0) {
      return new float[0];
    }
    return nativeGetSelectionRects(j, selectionBase, selectionExtent);
  }

  @Override // com.lynx.tasm.behavior.ui.text.TextEditContextSession
  public TextEditContextResult setSelectionFromPoint(
      AndroidText hostView, float x, float y, int anchor, long expectedRevision) {
    if (!refreshLayout(hostView)) {
      return new TextEditContextResult(false, false, snapshot());
    }
    int[] screen = new int[2];
    hostView.getLocationOnScreen(screen);
    return nativeSetSelectionFromPoint(
        this.mNativeLayoutSessionPtr, screen[0] + x, screen[1] + y, anchor, expectedRevision);
  }

  void invalidate() {
    long j = this.mNativeLayoutSessionPtr;
    if (j != 0) {
      nativeDestroyLayoutSession(j);
      this.mNativeLayoutSessionPtr = 0L;
    }
    this.mRegistryPtr = 0L;
    this.mHostView.clear();
  }
}
