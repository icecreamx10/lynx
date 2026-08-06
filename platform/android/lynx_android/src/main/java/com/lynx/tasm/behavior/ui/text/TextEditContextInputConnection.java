// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

import android.content.Context;
import android.graphics.Matrix;
import android.graphics.RectF;
import android.os.Build;
import android.text.Editable;
import android.text.Selection;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextUtils;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.CursorAnchorInfo;
import android.view.inputmethod.EditorBoundsInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.view.inputmethod.InputMethodManager;

final class TextEditContextInputConnection extends BaseInputConnection {
  private int mBatchDepth;
  private final Object mComposingSpan;
  private final SpannableStringBuilder mEditable;
  private final TextEditContextSession mSession;
  private final View mTargetView;
  private int mCursorUpdateFilter;
  private boolean mMonitorCursorUpdates;
  private TextEditContextSnapshot mPendingImeUpdate;

  TextEditContextInputConnection(View targetView, TextEditContextSession session) {
    super(targetView, true);
    this.mEditable = new SpannableStringBuilder();
    this.mComposingSpan = new Object();
    this.mTargetView = targetView;
    this.mSession = session;
    refreshEditable(session.snapshot());
  }

  @Override // android.view.inputmethod.BaseInputConnection
  public Editable getEditable() {
    refreshEditable(this.mSession.snapshot());
    return this.mEditable;
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public CharSequence getTextBeforeCursor(int length, int flags) {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return null;
    }
    int cursor = orderedSelectionStart(snapshot);
    return snapshot.text.substring(Math.max(0, cursor - Math.max(0, length)), cursor);
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public CharSequence getTextAfterCursor(int length, int flags) {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return null;
    }
    int cursor = orderedSelectionEnd(snapshot);
    return snapshot.text.substring(
        cursor, Math.min(snapshot.text.length(), Math.max(0, length) + cursor));
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public CharSequence getSelectedText(int flags) {
    int start;
    int end;
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null
        || (start = orderedSelectionStart(snapshot)) == (end = orderedSelectionEnd(snapshot))) {
      return null;
    }
    return snapshot.text.substring(start, end);
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public int getCursorCapsMode(int reqModes) {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return 0;
    }
    return TextUtils.getCapsMode(snapshot.text, orderedSelectionStart(snapshot), reqModes);
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return null;
    }
    ExtractedText result = new ExtractedText();
    result.text = snapshot.text;
    result.startOffset = 0;
    result.partialStartOffset = -1;
    result.partialEndOffset = -1;
    result.selectionStart = snapshot.selectionBase;
    result.selectionEnd = snapshot.selectionExtent;
    result.flags = snapshot.text.indexOf('\n') < 0 ? ExtractedText.FLAG_SINGLE_LINE : 0;
    return result;
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean commitText(CharSequence text, int newCursorPosition) {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return false;
    }
    String replacement = text == null ? "" : text.toString();
    int start = replacementStart(snapshot);
    int end = replacementEnd(snapshot);
    int cursor = cursorAfterReplacement(
        start, end, replacement.length(), newCursorPosition, snapshot.text.length());
    return apply(new TextEditContextSession.Transaction(
        "insertText", true, start, end, replacement, cursor, cursor, -1, -1, snapshot.revision));
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean setComposingText(CharSequence text, int newCursorPosition) {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return false;
    }
    String replacement = text == null ? "" : text.toString();
    int start = replacementStart(snapshot);
    int end = replacementEnd(snapshot);
    int cursor = cursorAfterReplacement(
        start, end, replacement.length(), newCursorPosition, snapshot.text.length());
    return apply(new TextEditContextSession.Transaction("insertCompositionText", true, start, end,
        replacement, cursor, cursor, start, start + replacement.length(), snapshot.revision));
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean setComposingRegion(int start, int end) {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return false;
    }
    int rangeStart = clamp(Math.min(start, end), 0, snapshot.text.length());
    int rangeEnd = clamp(Math.max(start, end), 0, snapshot.text.length());
    return apply(new TextEditContextSession.Transaction("", false, rangeStart, rangeEnd, "",
        snapshot.selectionBase, snapshot.selectionExtent, rangeStart, rangeEnd, snapshot.revision));
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean finishComposingText() {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return false;
    }
    return apply(new TextEditContextSession.Transaction("", false, 0, 0, "", snapshot.selectionBase,
        snapshot.selectionExtent, -1, -1, snapshot.revision));
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean setSelection(int start, int end) {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return false;
    }
    int base = clamp(start, 0, snapshot.text.length());
    int extent = clamp(end, 0, snapshot.text.length());
    return apply(new TextEditContextSession.Transaction("", false, 0, 0, "", base, extent,
        snapshot.compositionStart, snapshot.compositionEnd, snapshot.revision));
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean deleteSurroundingText(int beforeLength, int afterLength) {
    String str;
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return false;
    }
    int start = Math.max(0, orderedSelectionStart(snapshot) - Math.max(0, beforeLength));
    int end =
        Math.min(snapshot.text.length(), orderedSelectionEnd(snapshot) + Math.max(0, afterLength));
    if (beforeLength > 0 && afterLength == 0) {
      str = "deleteContentBackward";
    } else {
      str = (afterLength <= 0 || beforeLength != 0) ? "deleteContent" : "deleteContentForward";
    }
    return deleteRange(snapshot, start, end, str);
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
    String str;
    if (Build.VERSION.SDK_INT < 24) {
      return super.deleteSurroundingTextInCodePoints(beforeLength, afterLength);
    }
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return false;
    }
    int selectionStart = orderedSelectionStart(snapshot);
    int selectionEnd = orderedSelectionEnd(snapshot);
    int start = offsetByCodePoints(snapshot.text, selectionStart, -Math.max(0, beforeLength));
    int end = offsetByCodePoints(snapshot.text, selectionEnd, Math.max(0, afterLength));
    if (beforeLength > 0 && afterLength == 0) {
      str = "deleteContentBackward";
    } else {
      str = (afterLength <= 0 || beforeLength != 0) ? "deleteContent" : "deleteContentForward";
    }
    return deleteRange(snapshot, start, end, str);
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean sendKeyEvent(KeyEvent event) {
    if (event.getAction() != KeyEvent.ACTION_DOWN) {
      return true;
    }
    if (event.getKeyCode() == KeyEvent.KEYCODE_DEL) {
      return performInput("deleteContentBackward", "");
    }
    if (event.getKeyCode() == KeyEvent.KEYCODE_FORWARD_DEL) {
      return performInput("deleteContentForward", "");
    }
    if (event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {
      return commitText("\n", 1);
    }
    return super.sendKeyEvent(event);
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean beginBatchEdit() {
    this.mBatchDepth++;
    return true;
  }

  @Override
  // android.view.inputmethod.BaseInputConnection, android.view.inputmethod.InputConnection
  public boolean endBatchEdit() {
    int i = this.mBatchDepth;
    if (i > 0) {
      this.mBatchDepth = i - 1;
    }
    if (mBatchDepth == 0 && mPendingImeUpdate != null) {
      TextEditContextSnapshot pending = mPendingImeUpdate;
      mPendingImeUpdate = null;
      notifySelection(pending);
    }
    return true;
  }

  @Override
  // android.view.inputmethod.InputConnection
  public void closeConnection() {
    super.closeConnection();
    mBatchDepth = 0;
    mPendingImeUpdate = null;
    mMonitorCursorUpdates = false;
    mCursorUpdateFilter = 0;
  }

  @Override
  // android.view.inputmethod.InputConnection
  public boolean requestCursorUpdates(int cursorUpdateMode) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
      return false;
    }
    final int supportedModes = CURSOR_UPDATE_IMMEDIATE | CURSOR_UPDATE_MONITOR;
    final int supportedFilters = CURSOR_UPDATE_FILTER_CHARACTER_BOUNDS
        | CURSOR_UPDATE_FILTER_EDITOR_BOUNDS | CURSOR_UPDATE_FILTER_INSERTION_MARKER;
    if (cursorUpdateMode == 0) {
      mMonitorCursorUpdates = false;
      mCursorUpdateFilter = 0;
      return true;
    }
    if ((cursorUpdateMode & ~(supportedModes | supportedFilters)) != 0
        || (cursorUpdateMode & supportedModes) == 0) {
      return false;
    }
    mMonitorCursorUpdates = (cursorUpdateMode & CURSOR_UPDATE_MONITOR) != 0;
    mCursorUpdateFilter = cursorUpdateMode & supportedFilters;
    if ((cursorUpdateMode & CURSOR_UPDATE_IMMEDIATE) != 0) {
      updateCursorAnchorInfo(mSession.snapshot());
    }
    return true;
  }

  @Override
  // android.view.inputmethod.InputConnection
  public boolean requestCursorUpdates(int cursorUpdateMode, int cursorUpdateFilter) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
      return false;
    }
    final int supportedFilters = CURSOR_UPDATE_FILTER_CHARACTER_BOUNDS
        | CURSOR_UPDATE_FILTER_EDITOR_BOUNDS | CURSOR_UPDATE_FILTER_INSERTION_MARKER;
    if ((cursorUpdateFilter & ~supportedFilters) != 0) {
      return false;
    }
    if (cursorUpdateMode == 0) {
      return requestCursorUpdates(0);
    }
    return requestCursorUpdates(cursorUpdateMode | cursorUpdateFilter);
  }

  void onSessionStateChanged(TextEditContextSnapshot snapshot) {
    refreshEditable(snapshot);
    if (mBatchDepth > 0) {
      mPendingImeUpdate = snapshot;
    } else {
      notifySelection(snapshot);
    }
  }

  private boolean deleteRange(
      TextEditContextSnapshot snapshot, int start, int end, String inputType) {
    if (start == end) {
      return true;
    }
    return apply(new TextEditContextSession.Transaction(
        inputType, true, start, end, "", start, start, -1, -1, snapshot.revision));
  }

  private boolean apply(TextEditContextSession.Transaction transaction) {
    TextEditContextResult result = this.mSession.apply(transaction);
    return applyResult(result);
  }

  private boolean performInput(String inputType, String data) {
    TextEditContextSnapshot snapshot = this.mSession.snapshot();
    if (snapshot == null) {
      return false;
    }
    return applyResult(this.mSession.performInput(inputType, data, snapshot.revision));
  }

  private boolean applyResult(TextEditContextResult result) {
    if (result == null || !result.accepted) {
      if (result != null) {
        refreshEditable(result.snapshot);
        if (result.restartInput) {
          restartInput();
        }
      }
      return false;
    }
    refreshEditable(result.snapshot);
    if (mBatchDepth > 0) {
      mPendingImeUpdate = result.snapshot;
    } else {
      notifySelection(result.snapshot);
    }
    if (result.restartInput) {
      restartInput();
      return true;
    }
    return true;
  }

  private void refreshEditable(TextEditContextSnapshot snapshot) {
    if (snapshot == null) {
      this.mEditable.clear();
      return;
    }
    SpannableStringBuilder spannableStringBuilder = this.mEditable;
    spannableStringBuilder.replace(
        0, spannableStringBuilder.length(), (CharSequence) snapshot.text);
    int base = clamp(snapshot.selectionBase, 0, this.mEditable.length());
    int extent = clamp(snapshot.selectionExtent, 0, this.mEditable.length());
    Selection.setSelection(this.mEditable, base, extent);
    removeComposingSpans(this.mEditable);
    if (snapshot.isComposing()) {
      int start = clamp(snapshot.compositionStart, 0, this.mEditable.length());
      int end = clamp(snapshot.compositionEnd, start, this.mEditable.length());
      this.mEditable.setSpan(this.mComposingSpan, start, end,
          Spanned.SPAN_EXCLUSIVE_EXCLUSIVE | Spanned.SPAN_COMPOSING);
    }
  }

  private void notifySelection(TextEditContextSnapshot snapshot) {
    if (snapshot == null) {
      return;
    }
    InputMethodManager manager =
        (InputMethodManager) this.mTargetView.getContext().getSystemService(
            Context.INPUT_METHOD_SERVICE);
    if (manager != null) {
      manager.updateSelection(this.mTargetView, snapshot.selectionBase, snapshot.selectionExtent,
          snapshot.compositionStart, snapshot.compositionEnd);
    }
    View view = this.mTargetView;
    if (view instanceof AndroidText) {
      ((AndroidText) view).refreshTextEditContextGeometry();
      this.mTargetView.invalidate();
    }
    if (mMonitorCursorUpdates) {
      updateCursorAnchorInfo(snapshot);
    }
  }

  private void updateCursorAnchorInfo(TextEditContextSnapshot snapshot) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP || snapshot == null) {
      return;
    }
    if (mTargetView instanceof AndroidText) {
      mSession.refreshLayout((AndroidText) mTargetView);
    }

    CursorAnchorInfo.Builder builder = new CursorAnchorInfo.Builder();
    builder.setSelectionRange(snapshot.selectionBase, snapshot.selectionExtent);

    int[] windowLocation = new int[2];
    int[] screenLocation = new int[2];
    mTargetView.getLocationInWindow(windowLocation);
    mTargetView.getLocationOnScreen(screenLocation);
    Matrix localToScreen = new Matrix();
    localToScreen.setTranslate(screenLocation[0], screenLocation[1]);
    builder.setMatrix(localToScreen);
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
        && wantsCursorData(CURSOR_UPDATE_FILTER_EDITOR_BOUNDS)) {
      RectF editorBounds = new RectF(0, 0, mTargetView.getWidth(), mTargetView.getHeight());
      builder.setEditorBoundsInfo(
          new EditorBoundsInfo.Builder()
              .setEditorBounds(editorBounds)
              .setHandwritingBounds(editorBounds)
              .build());
    }

    int caret = clamp(snapshot.selectionExtent, 0, snapshot.text.length());
    float[] caretRect = mSession.selectionRects(caret, caret);
    if (wantsCursorData(CURSOR_UPDATE_FILTER_INSERTION_MARKER) && hasRect(caretRect, 0)) {
      float left = caretRect[0] - windowLocation[0];
      float top = caretRect[1] - windowLocation[1];
      float bottom = top + caretRect[3];
      builder.setInsertionMarkerLocation(left, top, bottom, bottom,
          CursorAnchorInfo.FLAG_HAS_VISIBLE_REGION);
    }

    if (snapshot.isComposing() && wantsCursorData(CURSOR_UPDATE_FILTER_CHARACTER_BOUNDS)) {
      int compositionStart = clamp(snapshot.compositionStart, 0, snapshot.text.length());
      int compositionEnd = clamp(snapshot.compositionEnd, compositionStart, snapshot.text.length());
      builder.setComposingText(
          compositionStart, snapshot.text.subSequence(compositionStart, compositionEnd));
      float[] characterRects = mSession.selectionRects(compositionStart, compositionEnd);
      int characterCount = Math.min(compositionEnd - compositionStart,
          characterRects == null ? 0 : characterRects.length / 4);
      for (int index = 0; index < characterCount; index++) {
        int rectOffset = index * 4;
        if (!hasRect(characterRects, rectOffset)) {
          continue;
        }
        float left = characterRects[rectOffset] - windowLocation[0];
        float top = characterRects[rectOffset + 1] - windowLocation[1];
        builder.addCharacterBounds(compositionStart + index, left, top,
            left + characterRects[rectOffset + 2], top + characterRects[rectOffset + 3],
            CursorAnchorInfo.FLAG_HAS_VISIBLE_REGION);
      }
    }

    InputMethodManager manager =
        (InputMethodManager)
            mTargetView.getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
    if (manager != null) {
      manager.updateCursorAnchorInfo(mTargetView, builder.build());
    }
  }

  private static boolean hasRect(float[] rects, int offset) {
    return rects != null && offset >= 0 && offset + 3 < rects.length;
  }

  private boolean wantsCursorData(int filter) {
    return mCursorUpdateFilter == 0 || (mCursorUpdateFilter & filter) != 0;
  }

  private void restartInput() {
    InputMethodManager manager =
        (InputMethodManager) this.mTargetView.getContext().getSystemService(
            Context.INPUT_METHOD_SERVICE);
    if (manager != null) {
      manager.restartInput(this.mTargetView);
    }
  }

  private static int replacementStart(TextEditContextSnapshot snapshot) {
    return snapshot.isComposing() ? snapshot.compositionStart : orderedSelectionStart(snapshot);
  }

  private static int replacementEnd(TextEditContextSnapshot snapshot) {
    return snapshot.isComposing() ? snapshot.compositionEnd : orderedSelectionEnd(snapshot);
  }

  private static int orderedSelectionStart(TextEditContextSnapshot snapshot) {
    return Math.min(snapshot.selectionBase, snapshot.selectionExtent);
  }

  private static int orderedSelectionEnd(TextEditContextSnapshot snapshot) {
    return Math.max(snapshot.selectionBase, snapshot.selectionExtent);
  }

  private static int cursorAfterReplacement(
      int start, int end, int replacementLength, int newCursorPosition, int oldTextLength) {
    int newTextLength = (oldTextLength - (end - start)) + replacementLength;
    int cursor = newCursorPosition > 0 ? ((start + replacementLength) + newCursorPosition) - 1
                                       : start + newCursorPosition;
    return clamp(cursor, 0, newTextLength);
  }

  private static int offsetByCodePoints(String text, int offset, int delta) {
    try {
      return text.offsetByCodePoints(offset, delta);
    } catch (IndexOutOfBoundsException e) {
      if (delta < 0) {
        return 0;
      }
      return text.length();
    }
  }

  private static int clamp(int value, int minimum, int maximum) {
    return Math.max(minimum, Math.min(maximum, value));
  }
}
