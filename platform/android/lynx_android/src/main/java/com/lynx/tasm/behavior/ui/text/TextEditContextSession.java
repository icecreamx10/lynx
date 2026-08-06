// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

interface TextEditContextSession {
  boolean activate();

  TextEditContextResult apply(Transaction transaction);

  void deactivate();

  boolean isActive();

  TextEditContextResult performInput(String str, String str2, long j);

  default boolean refreshLayout(AndroidText hostView) {
    return false;
  }

  default boolean refreshLayout(AndroidText hostView, int requestedStart, int requestedEnd) {
    return refreshLayout(hostView);
  }

  default float[] selectionRects(int selectionBase, int selectionExtent) {
    return new float[0];
  }

  default TextEditContextResult setSelectionFromPoint(
      AndroidText hostView, float x, float y, int anchor, long expectedRevision) {
    return new TextEditContextResult(TextEditContextResult.STATUS_INACTIVE, false, snapshot());
  }

  TextEditContextSnapshot snapshot();

  final class Transaction {
    final int compositionEnd;
    final int compositionStart;
    final long expectedRevision;
    final String inputType;
    final int rangeEnd;
    final int rangeStart;
    final String replacement;
    final int selectionBase;
    final int selectionExtent;
    final boolean updatesText;

    Transaction(String inputType, boolean updatesText, int rangeStart, int rangeEnd,
        String replacement, int selectionBase, int selectionExtent, int compositionStart,
        int compositionEnd, long expectedRevision) {
      this.inputType = inputType;
      this.updatesText = updatesText;
      this.rangeStart = rangeStart;
      this.rangeEnd = rangeEnd;
      this.replacement = replacement;
      this.selectionBase = selectionBase;
      this.selectionExtent = selectionExtent;
      this.compositionStart = compositionStart;
      this.compositionEnd = compositionEnd;
      this.expectedRevision = expectedRevision;
    }
  }
}
