// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class TextEditContextLayoutCollectorTest {
  @Test
  public void textUnitsFromDifferentOwnersKeepProjectionAndLocalOffsets() {
    TextEditContextLayoutData data =
        new TextEditContextLayoutData(3, 4, 2, 0, 2, 2, new float[4]);
    Layout first = layout("A");
    Layout second = layout("B");

    assertTrue(TextEditContextLayoutCollector.populateTextUnit(
        data, 0, 0, 10, 100, 0, first, 5, 7));
    assertTrue(TextEditContextLayoutCollector.populateTextUnit(
        data, 1, 1, 20, 200, 0, second, 25, 27));

    assertEquals(0, data.projectionOffsets[0]);
    assertEquals(1, data.projectionOffsets[1]);
    assertEquals(100, data.ownerIds[0]);
    assertEquals(200, data.ownerIds[1]);
    assertEquals(10, data.segmentIds[0]);
    assertEquals(20, data.segmentIds[1]);
    assertTrue(data.bounds[4] >= 25);
  }

  @Test
  public void blockAtomIsIdentifiedByAdjacentBoundaryWithSameSegmentId() {
    TextEditContextLayoutSnapshot projection = new TextEditContextLayoutSnapshot(1, 2, 3,
        new long[] {30, 30, 30}, new long[] {-1, -1, -1},
        new int[] {TextEditContextLayoutSnapshot.KIND_BLOCK_BOUNDARY,
            TextEditContextLayoutSnapshot.KIND_ATOMIC_OBJECT,
            TextEditContextLayoutSnapshot.KIND_BLOCK_BOUNDARY},
        new int[] {0, 1, 2}, new int[] {1, 2, 3}, new int[] {1, 0, 2},
        new String[] {"\n", "\uFFFC", "\n"});

    assertTrue(TextEditContextLayoutCollector.isBlockAtomic(projection, 1));
  }

  @Test
  public void repeatedSegmentsOnSameOwnerAdvanceFromPreviousMatch() {
    String renderedText = "repeat-repeat-repeat";
    int first = TextEditContextLayoutCollector.findTextOffset(renderedText, "repeat", 0);
    int second =
        TextEditContextLayoutCollector.findTextOffset(renderedText, "repeat", first + 6);
    int third =
        TextEditContextLayoutCollector.findTextOffset(renderedText, "repeat", second + 6);

    assertEquals(0, first);
    assertEquals(7, second);
    assertEquals(14, third);
  }

  @SuppressWarnings("deprecation")
  private static Layout layout(String text) {
    return new StaticLayout(
        text, new TextPaint(), 200, Layout.Alignment.ALIGN_NORMAL, 1, 0, false);
  }
}
