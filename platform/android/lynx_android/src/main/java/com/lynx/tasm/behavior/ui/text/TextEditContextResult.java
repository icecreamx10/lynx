// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

final class TextEditContextResult {
  final boolean accepted;
  final boolean restartInput;
  final TextEditContextSnapshot snapshot;

  TextEditContextResult(boolean accepted, boolean restartInput, TextEditContextSnapshot snapshot) {
    this.accepted = accepted;
    this.restartInput = restartInput;
    this.snapshot = snapshot;
  }
}
