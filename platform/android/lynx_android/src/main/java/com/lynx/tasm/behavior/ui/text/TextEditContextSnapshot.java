// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

final class TextEditContextSnapshot {
  final int compositionEnd;
  final int compositionStart;
  final long revision;
  final int selectionBase;
  final int selectionExtent;
  final String text;

  TextEditContextSnapshot(String text, int selectionBase, int selectionExtent, int compositionStart,
      int compositionEnd, long revision) {
    this.text = text;
    this.selectionBase = selectionBase;
    this.selectionExtent = selectionExtent;
    this.compositionStart = compositionStart;
    this.compositionEnd = compositionEnd;
    this.revision = revision;
  }

  boolean isComposing() {
    int i = this.compositionStart;
    return i >= 0 && this.compositionEnd >= i;
  }
}
