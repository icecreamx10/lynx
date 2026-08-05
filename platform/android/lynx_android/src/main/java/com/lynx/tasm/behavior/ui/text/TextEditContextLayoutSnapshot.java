// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

final class TextEditContextLayoutSnapshot {
  static final int KIND_ATOMIC_OBJECT = 2;
  static final int KIND_BLOCK_BOUNDARY = 1;
  static final int KIND_TEXT = 0;
  final int[] boundaryEdges;
  final int[] ends;
  final int[] kinds;
  final int length;
  final long[] ownerIds;
  final long projectionRevision;
  final long[] segmentIds;
  final long stateRevision;
  final int[] starts;
  final String[] texts;

  TextEditContextLayoutSnapshot(long stateRevision, long projectionRevision, int length,
      long[] segmentIds, long[] ownerIds, int[] kinds, int[] starts, int[] ends,
      int[] boundaryEdges, String[] texts) {
    this.stateRevision = stateRevision;
    this.projectionRevision = projectionRevision;
    this.length = length;
    this.segmentIds = segmentIds;
    this.ownerIds = ownerIds;
    this.kinds = kinds;
    this.starts = starts;
    this.ends = ends;
    this.boundaryEdges = boundaryEdges;
    this.texts = texts;
  }

  int segmentCount() {
    return this.segmentIds.length;
  }
}
