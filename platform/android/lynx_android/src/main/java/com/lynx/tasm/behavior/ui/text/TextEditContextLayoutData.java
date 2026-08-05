// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

final class TextEditContextLayoutData {
  final int[] boundaryEdges;
  final float[] bounds;
  final float[] controlBounds;
  final int[] flags;
  final int[] localEnds;
  final int[] localStarts;
  final long[] ownerIds;
  final long revision;
  final long[] segmentIds;

  TextEditContextLayoutData(long revision, int unitCount, float[] controlBounds) {
    this.revision = revision;
    this.controlBounds = controlBounds;
    this.segmentIds = new long[unitCount];
    this.ownerIds = new long[unitCount];
    this.localStarts = new int[unitCount];
    this.localEnds = new int[unitCount];
    this.bounds = new float[unitCount * 4];
    this.flags = new int[unitCount];
    this.boundaryEdges = new int[unitCount];
  }
}
