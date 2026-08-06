// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

import android.graphics.Rect;
import android.text.Layout;
import com.lynx.tasm.behavior.LynxContext;
import com.lynx.tasm.behavior.LynxUIOwner;
import com.lynx.tasm.behavior.ui.LynxBaseUI;

final class TextEditContextLayoutCollector {
  private static final int FLAG_ATOMIC = 4;
  private static final int FLAG_BLOCK = 8;
  private static final int FLAG_HAS_DIRECTION = 1;
  private static final int FLAG_RIGHT_TO_LEFT = 2;
  private static final long INVALID_OWNER_ID = -1;

  private static final class TextOwner {
    final Layout layout;
    final float originX;
    final float originY;

    TextOwner(Layout layout, float originX, float originY) {
      this.layout = layout;
      this.originX = originX;
      this.originY = originY;
    }
  }

  private TextEditContextLayoutCollector() {}

  static TextEditContextLayoutData collect(
      AndroidText hostView, TextEditContextLayoutSnapshot projection) {
    return collect(hostView, projection, -1, -1);
  }

  static TextEditContextLayoutData collect(AndroidText hostView,
      TextEditContextLayoutSnapshot projection, int requestedStart, int requestedEnd) {
    LynxContext context =
        hostView.getContext() instanceof LynxContext ? (LynxContext) hostView.getContext() : null;
    LynxUIOwner owner = context == null ? null : context.getLynxUIOwner();
    if (owner == null || !isWellFormed(projection)) {
      return null;
    }
    int segmentCount = projection.segmentCount();
    int coverageFirstSegment = nearestMeasurableSegment(
        owner, projection, requestedStart, requestedEnd);
    int coverageLastSegment = coverageFirstSegment;
    if (coverageFirstSegment >= 0) {
      while (coverageFirstSegment > 0
          && canMeasureSegment(owner, projection, coverageFirstSegment - 1)) {
        coverageFirstSegment--;
      }
      while (coverageLastSegment + 1 < segmentCount
          && canMeasureSegment(owner, projection, coverageLastSegment + 1)) {
        coverageLastSegment++;
      }
    }
    int coverageStart = coverageFirstSegment < 0 ? 0 : projection.starts[coverageFirstSegment];
    int coverageEnd = coverageLastSegment < 0 ? 0 : projection.ends[coverageLastSegment];
    int unitCount = coverageEnd - coverageStart;
    int[] hostScreen = new int[2];
    hostView.getLocationInWindow(hostScreen);
    TextEditContextLayoutData result = new TextEditContextLayoutData(projection.stateRevision,
        projection.projectionRevision, projection.length, coverageStart, coverageEnd, unitCount,
        new float[] {hostScreen[0], hostScreen[1], hostView.getWidth(), hostView.getHeight()});
    int[] ownerCursors = new int[segmentCount];
    long[] cursorOwnerIds = new long[segmentCount];
    int cursorCount = 0;

    for (int segmentIndex = Math.max(0, coverageFirstSegment); segmentIndex <= coverageLastSegment;
         segmentIndex++) {
      int start = projection.starts[segmentIndex];
      int end = projection.ends[segmentIndex];
      int kind = projection.kinds[segmentIndex];
      if (start < coverageStart || end < start || end > coverageEnd) {
        return null;
      }
      if (kind != 0) {
        if (!populateAtomicUnit(result, projection, owner, segmentIndex, start - coverageStart,
                end - coverageStart, start, kind)) {
          return null;
        }
        continue;
      }

      long ownerId = projection.ownerIds[segmentIndex];
      TextOwner textOwner = findTextOwner(owner, ownerId);
      if (textOwner == null) {
        return null;
      }
      String needle = projection.texts[segmentIndex];
      if (end - start != needle.length()) {
        return null;
      }
      int cursorIndex = findCursorOwner(cursorOwnerIds, cursorCount, ownerId);
      int searchStart = cursorIndex < 0 ? 0 : ownerCursors[cursorIndex];
      String renderedText = textOwner.layout.getText().toString();
      int rawStart = findTextOffset(renderedText, needle, searchStart);
      if (rawStart < 0) {
        return null;
      }
      if (cursorIndex < 0) {
        cursorIndex = cursorCount++;
        cursorOwnerIds[cursorIndex] = ownerId;
      }
      ownerCursors[cursorIndex] = rawStart + needle.length();
      for (int offset = 0; offset < needle.length(); offset++) {
        if (!populateTextUnit(result, start + offset - coverageStart, start + offset,
                projection.segmentIds[segmentIndex], ownerId, rawStart + offset, textOwner.layout,
                textOwner.originX, textOwner.originY)) {
          return null;
        }
      }
    }
    return result;
  }

  static int findTextOffset(String renderedText, String segmentText, int searchStart) {
    int offset = renderedText.indexOf(segmentText, Math.max(0, searchStart));
    return offset >= 0 ? offset : renderedText.indexOf(segmentText);
  }

  private static int nearestMeasurableSegment(LynxUIOwner owner,
      TextEditContextLayoutSnapshot projection, int requestedStart, int requestedEnd) {
    int bestIndex = -1;
    int bestDistance = Integer.MAX_VALUE;
    int target = requestedStart < 0 ? 0 : Math.min(requestedStart, requestedEnd);
    for (int index = 0; index < projection.segmentCount(); index++) {
      if (!canMeasureSegment(owner, projection, index)) {
        continue;
      }
      if (requestedStart < 0) {
        return index;
      }
      int start = projection.starts[index];
      int end = projection.ends[index];
      int distance = target < start ? start - target : (target > end ? target - end : 0);
      if (distance < bestDistance) {
        bestDistance = distance;
        bestIndex = index;
      }
    }
    return bestIndex;
  }

  private static boolean canMeasureSegment(
      LynxUIOwner owner, TextEditContextLayoutSnapshot projection, int segmentIndex) {
    if (projection.kinds[segmentIndex] == TextEditContextLayoutSnapshot.KIND_TEXT) {
      return findTextOwner(owner, projection.ownerIds[segmentIndex]) != null;
    }
    return owner.findLynxUIBySign((int) projection.segmentIds[segmentIndex]) != null;
  }

  private static boolean populateAtomicUnit(TextEditContextLayoutData result,
      TextEditContextLayoutSnapshot projection, LynxUIOwner owner, int segmentIndex, int start,
      int end, int projectionOffset, int kind) {
    LynxBaseUI segmentUI = owner.findLynxUIBySign((int) projection.segmentIds[segmentIndex]);
    if (segmentUI == null || end - start != 1) {
      return false;
    }
    Rect bounds = segmentUI.getRectToWindow();
    result.projectionOffsets[start] = projectionOffset;
    result.segmentIds[start] = projection.segmentIds[segmentIndex];
    result.ownerIds[start] = INVALID_OWNER_ID;
    result.localStarts[start] = 0;
    result.localEnds[start] = 0;
    result.boundaryEdges[start] = projection.boundaryEdges[segmentIndex];
    if (kind == 2) {
      setBounds(result, start, bounds.left, bounds.top, Math.max(1, bounds.width()),
          Math.max(1, bounds.height()));
      result.flags[start] =
          (isBlockAtomic(projection, segmentIndex) ? FLAG_BLOCK : 0) | FLAG_ATOMIC;
    } else {
      boolean leading = projection.boundaryEdges[segmentIndex] == 1;
      setBounds(result, start, leading ? bounds.left : bounds.right,
          leading ? bounds.top : bounds.bottom, 1.0f, 1.0f);
    }
    return true;
  }

  static boolean isBlockAtomic(TextEditContextLayoutSnapshot projection, int segmentIndex) {
    if (segmentIndex < 0 || segmentIndex >= projection.segmentCount()
        || projection.kinds[segmentIndex] != 2) {
      return false;
    }
    long segmentId = projection.segmentIds[segmentIndex];
    for (int index = Math.max(0, segmentIndex - 1);
         index <= Math.min(projection.segmentCount() - 1, segmentIndex + 1); index++) {
      if (index != segmentIndex && projection.segmentIds[index] == segmentId
          && projection.kinds[index] == 1) {
        return true;
      }
    }
    return false;
  }

  private static boolean isWellFormed(TextEditContextLayoutSnapshot projection) {
    int count = projection.segmentCount();
    return projection.ownerIds.length == count && projection.kinds.length == count
        && projection.starts.length == count && projection.ends.length == count
        && projection.boundaryEdges.length == count && projection.texts.length == count;
  }

  private static int findCursorOwner(long[] ownerIds, int count, long ownerId) {
    for (int index = 0; index < count; index++) {
      if (ownerIds[index] == ownerId) {
        return index;
      }
    }
    return -1;
  }

  private static TextOwner findTextOwner(LynxUIOwner uiOwner, long ownerId) {
    FlattenUIText text;
    Layout layout;
    LynxBaseUI ui = uiOwner.findLynxUIBySign((int) ownerId);
    if (ui instanceof UIText) {
      AndroidText view = (AndroidText) ((UIText) ui).getView();
      Layout layout2 = view == null ? null : view.getTextLayout();
      if (layout2 == null) {
        return null;
      }
      int[] screen = new int[2];
      view.getLocationInWindow(screen);
      return new TextOwner(
          layout2, screen[0] + view.getTextDrawOffsetX(), screen[1] + view.getTextDrawOffsetY());
    }
    if (!(ui instanceof FlattenUIText)
        || (layout = (text = (FlattenUIText) ui).getTextLayout()) == null) {
      return null;
    }
    Rect rect = text.getRectToWindow();
    return new TextOwner(
        layout, rect.left + text.getDrawOffsetLeft(), rect.top + text.getDrawOffsetTop());
  }

  static boolean populateTextUnit(TextEditContextLayoutData result, int unitIndex,
      int projectionOffset, long segmentId, long ownerId, int localOffset, Layout layout,
      float originX, float originY) {
    float endX;
    float lineLeft;
    CharSequence text = layout.getText();
    if (localOffset < 0 || localOffset >= text.length()) {
      return false;
    }
    int line = layout.getLineForOffset(localOffset);
    int nextLine = layout.getLineForOffset(localOffset + 1);
    boolean rightToLeft = layout.isRtlCharAt(localOffset);
    float startX = layout.getPrimaryHorizontal(localOffset);
    float endX2 = layout.getPrimaryHorizontal(localOffset + 1);
    if (nextLine == line) {
      endX = endX2;
    } else {
      char character = text.charAt(localOffset);
      if (character == '\n' || character == '\r') {
        lineLeft = startX;
      } else {
        lineLeft = rightToLeft ? layout.getLineLeft(line) : layout.getLineRight(line);
      }
      float endX3 = lineLeft;
      endX = endX3;
    }
    float left = originX + Math.min(startX, endX);
    float top = originY + layout.getLineTop(line);
    float width = Math.max(1.0f, Math.abs(endX - startX));
    float height = Math.max(1, layout.getLineBottom(line) - layout.getLineTop(line));
    result.projectionOffsets[unitIndex] = projectionOffset;
    result.segmentIds[unitIndex] = segmentId;
    result.ownerIds[unitIndex] = ownerId;
    result.localStarts[unitIndex] = localOffset;
    result.localEnds[unitIndex] = localOffset + 1;
    setBounds(result, unitIndex, left, top, width, height);
    result.flags[unitIndex] = (rightToLeft ? 2 : 0) | 1;
    return true;
  }

  private static void setBounds(
      TextEditContextLayoutData result, int index, float x, float y, float width, float height) {
    int offset = index * 4;
    result.bounds[offset] = x;
    result.bounds[offset + 1] = y;
    result.bounds[offset + 2] = width;
    result.bounds[offset + 3] = height;
  }
}
