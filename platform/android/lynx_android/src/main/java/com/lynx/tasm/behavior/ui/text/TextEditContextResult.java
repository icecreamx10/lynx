// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

final class TextEditContextResult {
  static final int STATUS_ACCEPTED = 0;
  static final int STATUS_INACTIVE = 1;
  static final int STATUS_STALE_REVISION = 2;
  static final int STATUS_INVALID_RANGE = 3;
  static final int STATUS_UNSUPPORTED_INPUT_TYPE = 4;
  static final int STATUS_GEOMETRY_UNAVAILABLE = 5;

  final boolean accepted;
  final boolean restartInput;
  final TextEditContextSnapshot snapshot;
  final int status;

  TextEditContextResult(int status, boolean restartInput, TextEditContextSnapshot snapshot) {
    this.status = status;
    this.accepted = status == STATUS_ACCEPTED;
    this.restartInput = restartInput;
    this.snapshot = snapshot;
  }
}
