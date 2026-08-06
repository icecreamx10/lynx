// Copyright 2019 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
package com.lynx.tasm.behavior.ui.text;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Picture;
import android.graphics.PointF;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.os.RemoteException;
import android.text.InputType;
import android.text.Layout;
import android.text.Selection;
import android.text.Spannable;
import android.text.Spanned;
import android.view.ActionMode;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import androidx.annotation.ColorInt;
import androidx.annotation.Keep;
import androidx.annotation.Nullable;
import com.lynx.R;
import com.lynx.react.bridge.ReadableArray;
import com.lynx.tasm.base.LLog;
import com.lynx.tasm.behavior.LynxContext;
import com.lynx.tasm.behavior.shadow.text.TextHelper;
import com.lynx.tasm.behavior.shadow.text.TextUpdateBundle;
import com.lynx.tasm.behavior.ui.view.AndroidView;
import com.lynx.tasm.event.LynxDetailEvent;
import com.lynx.tasm.service.ILynxSystemInvokeService;
import com.lynx.tasm.service.ILynxTextService.Page;
import com.lynx.tasm.service.LynxServiceCenter;
import com.lynx.tasm.utils.UIThreadUtils;
import java.lang.ref.WeakReference;
import java.text.BreakIterator;
import java.util.ArrayList;

@Keep
public class AndroidText extends AndroidView implements ActionMode.Callback {
  // integer id for action-menu-item copy
  private static final int ID_COPY = 0xFFFE;
  // integer id for action-menu-item select-all
  private static final int ID_SELECT_ALL = 0xFFFD;
  protected static final String SELECTION_CHANGE_EVENT = "selectionchange";
  private static final float RESPONSE_TOUCH_RADIUS = 50.f;
  private static final int DEFAULT_TEXT_SELECTION_COLOR = 0x6633B5E5;
  private static final int DEFAULT_TEXT_HANDLE_COLOR = 0xFF027AFB;
  private static final int DEFAULT_TEXT_HANDLE_SIZE = 15;

  protected Layout mTextLayout;
  protected TextUpdateBundle mTextUpdateBundle;
  protected PointF mTextTranslateOffset;
  protected boolean mHasImage;
  protected boolean mIsJustify;
  protected Page mTextraPage;
  private int mTextServiceTextLength = 0;
  private Picture mOverflowPicture;
  private int mOverflow;
  private boolean mOverflowPictureDirty;
  private CharSequence mOriginText;
  private boolean mNeedDrawStroke = false;

  private boolean mIsBindSelectionChange = false;
  private int mSign = 0;
  // select direction
  private boolean mIsForward = true;
  private boolean mEnableTextSelection = false;
  private boolean mEnableCustomContextMenu = false;
  private boolean mEnableCustomTextSelection = false;
  private Path mHighlightPath;
  private Paint mHighlightPaint;
  private int mTextSelectionColor;
  private int mTextSelectionHandleColor;
  private int mHandleSize;
  private int mDefaultHandlePlatformLength;
  private Drawable mSelectionLeftCursor;
  private Drawable mSelectionRightCursor;
  private ActionMode mActionMode = null;
  private int mSelectStart = -1;
  private int mSelectEnd = -1;
  private int mLastSelectStart = -1;
  private int mLastSelectEnd = -1;
  private final PointF mSelectStartPos = new PointF(-1.f, -1.f);
  private final PointF mSelectEndPos = new PointF(-1.f, -1.f);
  private final PointF mStartHandlerPos = new PointF(-1.f, -1.f);
  private final PointF mEndHandlerPos = new PointF(-1.f, -1.f);
  private boolean mIsInSelection = false;
  private boolean mIsAdjustStartPos = false;
  private boolean mIsAdjustEndPos = false;
  private CheckForLongPress mCheckForLongPress = null;
  private boolean mShouldResponseMove = false;
  private boolean mIsShowStartHandle = true;
  private boolean mIsShowEndHandle = true;
  private boolean mDisplayNone = false;
  private final ArrayList<RectF> mTextServiceSelectionBoxes = new ArrayList<>();
  private int mTextServiceSelectionStart = -1;
  private int mTextServiceSelectionEnd = -1;

  // save weak reference of selecting AndroidText to ensure that only one AndroidText is selected at
  // a time.
  private static WeakReference<AndroidText> sWeakSelectingAndroidText;
  private static WeakReference<AndroidText> sActiveTextEditContextHost;

  private TextEditContextSession mTextEditContextSession;
  private TextEditContextInputConnection mTextEditContextInputConnection;
  private final ArrayList<RectF> mEditContextSelectionRects = new ArrayList<>();
  private final Paint mEditContextSelectionPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint mEditContextCaretPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
  private boolean mEditContextSelectionCollapsed = true;
  private int mEditContextSelectionStart = -1;
  private int mEditContextSelectionEnd = -1;
  private final PointF mEditContextStartHandle = new PointF(-1.f, -1.f);
  private final PointF mEditContextEndHandle = new PointF(-1.f, -1.f);
  private int mEditContextTouchAnchor = -1;
  private long mLastEditContextTouchEventTime = Long.MIN_VALUE;
  private int mLastEditContextTouchAction = -1;
  private boolean mEditContextRootTouchActive;
  private boolean mEditContextIgnoreTouchSequence;
  private boolean mEditContextAdjustingStartHandle;
  private boolean mEditContextAdjustingEndHandle;
  private boolean mEditContextLongPressTriggered;
  private Runnable mEditContextLongPressRunnable;
  private float mEditContextTouchDownX;
  private float mEditContextTouchDownY;

  private final class CheckForLongPress implements Runnable {
    private final float mX;
    private final float mY;

    public CheckForLongPress(float x, float y) {
      mX = x;
      mY = y;
    }

    @Override
    public void run() {
      int offset = getOffsetForPosition(mX, mY);
      if (offset < 0) {
        mCheckForLongPress = null;
        return;
      }
      mIsInSelection = true;
      mSelectStartPos.set(mX, mY);
      mSelectEndPos.set(mX, mY);
      mSelectEnd = mSelectStart = offset;
      mIsAdjustEndPos = true;
      requestDisallowInterceptTouchEvent(true);

      // clear self
      mCheckForLongPress = null;
    }
  }

  public AndroidText(Context context) {
    super(context);
    mOverflowPicture = new Picture();
    mOverflow = 0x00;
    mOverflowPictureDirty = true;
    setFocusable(true);
    // can draw
    setWillNotDraw(false);
    mTextSelectionColor = DEFAULT_TEXT_SELECTION_COLOR;
    mTextSelectionHandleColor = DEFAULT_TEXT_HANDLE_COLOR;
    mEditContextSelectionPaint.setColor(DEFAULT_TEXT_SELECTION_COLOR);
    mEditContextCaretPaint.setColor(DEFAULT_TEXT_HANDLE_COLOR);
    mHandleSize = mDefaultHandlePlatformLength =
        Math.round(((LynxContext) context).getScreenMetrics().density * DEFAULT_TEXT_HANDLE_SIZE);
  }

  void setTextEditContextSession(TextEditContextSession session) {
    if (mTextEditContextSession == session) {
      return;
    }
    TextEditContextSession previousSession = mTextEditContextSession;
    if (previousSession != null) {
      if (session == null) {
        previousSession.deactivate();
        deactivateTextEditContext();
      } else {
        previousSession.deactivate();
      }
    }
    mTextEditContextSession = session;
    mTextEditContextInputConnection = null;
    if (previousSession instanceof TextEditContextSessionBridge) {
      ((TextEditContextSessionBridge) previousSession).invalidate();
    }
    mEditContextSelectionRects.clear();
    mEditContextTouchAnchor = -1;
    setFocusableInTouchMode(session != null);
    if (mTextEditContextSession != null && hasFocus()) {
      mTextEditContextSession.activate();
      sActiveTextEditContextHost = new WeakReference<>(this);
    }
    InputMethodManager manager =
        (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
    if (manager != null) {
      manager.restartInput(this);
    }
  }

  boolean isTextEditContextSession(TextEditContextSession session) {
    return mTextEditContextSession == session;
  }

  void activateTextEditContext() {
    if (mTextEditContextSession == null || (!requestFocus() && !hasFocus())) {
      return;
    }
    sActiveTextEditContextHost = new WeakReference<>(this);
    InputMethodManager manager =
        (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
    if (manager != null) {
      manager.restartInput(this);
      post(() -> {
        if (mTextEditContextSession != null && mTextEditContextSession.isActive() && hasFocus()) {
          manager.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT);
        }
      });
    }
  }

  void deactivateTextEditContext() {
    if (hasFocus()) {
      clearFocus();
    }
    InputMethodManager manager =
        (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
    if (manager != null) {
      manager.hideSoftInputFromWindow(getWindowToken(), 0);
    }
  }

  @Override
  public boolean onCheckIsTextEditor() {
    return mTextEditContextSession != null && mTextEditContextSession.isActive();
  }

  @Override
  public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
    TextEditContextSession session = mTextEditContextSession;
    TextEditContextSnapshot snapshot = session == null ? null : session.snapshot();
    if (session == null || !session.isActive() || snapshot == null) {
      return null;
    }
    outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE;
    outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI;
    outAttrs.initialSelStart = snapshot.selectionBase;
    outAttrs.initialSelEnd = snapshot.selectionExtent;
    mTextEditContextInputConnection = new TextEditContextInputConnection(this, session);
    return mTextEditContextInputConnection;
  }

  @Override
  protected void onFocusChanged(boolean gainFocus, int direction, Rect previouslyFocusedRect) {
    super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
    if (mTextEditContextSession == null) {
      return;
    }
    if (gainFocus) {
      mTextEditContextSession.activate();
      sActiveTextEditContextHost = new WeakReference<>(this);
    } else {
      mTextEditContextSession.deactivate();
      AndroidText activeHost =
          sActiveTextEditContextHost == null ? null : sActiveTextEditContextHost.get();
      if (activeHost == this) {
        sActiveTextEditContextHost = null;
      }
    }
  }

  public void setDisplayNone(boolean displayNone) {
    mDisplayNone = displayNone;
  }

  public void setTextBundle(TextUpdateBundle bundle) {
    // First detach old image span
    dispatchDetachImageSpan();
    mTextraPage = null;
    mTextServiceTextLength = 0;
    mTextUpdateBundle = bundle;
    mTextLayout = generateTextLayout(bundle);
    mTextTranslateOffset = bundle.getTextTranslateOffset();
    mHasImage = bundle.hasImages();
    mNeedDrawStroke = bundle.getNeedDrawStroke();
    mIsJustify = bundle.isJustify();
    mOriginText = bundle.getOriginText();
    if (mHasImage && getText() instanceof Spanned) {
      Spanned spannable = (Spanned) getText();
      AbsInlineImageSpan.possiblyUpdateInlineImageSpans(spannable, this);
    }
    if (mIsInSelection) {
      clearSelection();
    } else {
      resetSelectionState();
    }
    // Enable layout inspector to collect content
    setContentDescription(mTextLayout.getText());
    invalidate();
    mOverflowPictureDirty = true;
  }

  public void setTextBundle(Page page) {
    dispatchDetachImageSpan();
    mTextraPage = page;
    mTextServiceTextLength = page != null ? page.getTextLength() : 0;
    mTextUpdateBundle = null;
    mTextLayout = null;
    mTextTranslateOffset = new PointF();
    mHasImage = false;
    mNeedDrawStroke = false;
    mIsJustify = false;
    mOriginText = null;
    if (mIsInSelection) {
      clearSelection();
    } else {
      resetSelectionState();
    }
    setContentDescription(null);
    invalidate();
    mOverflowPictureDirty = true;
  }

  private void resetSelectionState() {
    mSelectStart = mSelectEnd = mLastSelectStart = mLastSelectEnd = -1;
    mIsInSelection = mIsAdjustStartPos = mIsAdjustEndPos = mShouldResponseMove = false;
    mIsShowStartHandle = mIsShowEndHandle = true;
    mSelectStartPos.set(-1.f, -1.f);
    mSelectEndPos.set(-1.f, -1.f);
    invalidateTextServiceSelectionBoxes();
  }

  public CharSequence getOriginText() {
    return mOriginText;
  }

  protected Layout generateTextLayout(TextUpdateBundle bundle) {
    return bundle.getTextLayout();
  }

  @Deprecated
  public void setTextGradient(String gradient) {
    LLog.e(
        "text-gradient", "setTextGradient(String) is deprecated, call this function has no effect");
  }

  public void setTextGradient(ReadableArray gradient) {}

  public void setEnableTextSelection(boolean enable) {
    mEnableTextSelection = enable;
  }

  public void updateSelectionBackgroundColor(int color) {
    mTextSelectionColor = color == Color.TRANSPARENT ? DEFAULT_TEXT_SELECTION_COLOR : color;
  }

  public void updateSelectionHandleColor(int color) {
    mTextSelectionHandleColor = color == Color.TRANSPARENT ? DEFAULT_TEXT_HANDLE_COLOR : color;
  }

  public void updateSelectionHandleSize(int size) {
    mHandleSize = size <= 0 ? mDefaultHandlePlatformLength : size;
  }

  @Keep
  @Override
  protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
    setMeasuredDimension(
        MeasureSpec.getSize(widthMeasureSpec), MeasureSpec.getSize(heightMeasureSpec));
  }

  @Keep
  @Override
  protected void onLayout(boolean changed, int l, int t, int r, int b) {
    if (super.getRenderer() != null) {
      if (super.getRenderer().getUIHost() != null) {
        super.getRenderer().getUIHost().measure();
      }
      super.getRenderer().onLayout(changed, l, t, r, b);
    }
  }

  @Override
  public boolean onTouchEvent(MotionEvent event) {
    if (mTextEditContextSession != null) {
      if (mEditContextIgnoreTouchSequence) {
        if (event.getActionMasked() == MotionEvent.ACTION_UP
            || event.getActionMasked() == MotionEvent.ACTION_CANCEL) {
          resetTextEditContextTouchGesture();
        }
        return true;
      }
      if (event.getAction() == MotionEvent.ACTION_DOWN && !hasFocus()) {
        activateTextEditContext();
      }
      if (handleTextEditContextTouch(event)) {
        return true;
      }
    }
    if (!hasTextSelectionContent() || !mEnableTextSelection || mEnableCustomTextSelection) {
      return super.onTouchEvent(event);
    }
    float x = event.getX() - getPaddingLeft();
    float y = event.getY() - getPaddingTop();

    if (event.getAction() == MotionEvent.ACTION_DOWN) {
      performBeginSelection(x, y);
    } else if (event.getAction() == MotionEvent.ACTION_MOVE) {
      performMovingSelection(x, y);
    } else if (event.getAction() == MotionEvent.ACTION_UP) {
      performEndSelection(x, y);
    } else {
      if (mShouldResponseMove) {
        performEndSelection(x, y);
      } else {
        // The long press callback needs to be removed when the finger leaves the screen.
        removeCheckLongPressCallback();
      }
    }

    invalidate();
    return true;
  }

  @Keep
  @Override
  protected void onDraw(Canvas canvas) {
    if (mDisplayNone) {
      return;
    }
    if (super.getRenderer() != null) {
      super.getRenderer().onDraw(canvas);
      if (mTextraPage != null) {
        drawHighlightWithTextOffset(canvas);
      }
      return;
    }

    if (mTextraPage != null) {
      int paddingLeft = getPaddingLeft();
      int paddingRight = getPaddingRight();
      int paddingTop = getPaddingTop();
      int paddingBottom = getPaddingBottom();
      canvas.save();
      if (mOverflow == 0) {
        canvas.clipRect(
            paddingLeft, paddingTop, getWidth() - paddingRight, getHeight() - paddingBottom);
      }
      canvas.translate(paddingLeft, paddingTop);
      drawHighlight(canvas);
      mTextraPage.drawPageCanvas(canvas, this);
      canvas.restore();
      return;
    }

    if (mTextLayout != null) {
      canvas.save();
      // since TextRender only build StaticLayout once
      // UIText needs to make translate to make sure content display in correct place
      // If layout not align left, needs to do offset
      canvas.translate(
          getPaddingLeft() + mTextTranslateOffset.x, getPaddingTop() + mTextTranslateOffset.y);

      if (mOverflow != 0) {
        drawHighlight(canvas);
        drawOverflowPicture();
        canvas.drawPicture(mOverflowPicture);
      } else {
        drawText(canvas);
      }
      canvas.restore();
    }
  }

  @Override
  public void dispatchDraw(Canvas canvas) {
    if (mDisplayNone) {
      return;
    }
    super.dispatchDraw(canvas);
    refreshTextEditContextGeometry();
    drawTextEditContextSelection(canvas);
    if (mTextraPage != null) {
      drawTextServiceSelectHandle(canvas);
      return;
    }

    if (super.getRenderer() != null) {
      return;
    }

    if (!mIsInSelection || mHighlightPath == null || mHighlightPath.isEmpty()) {
      return;
    }
    canvas.save();
    canvas.translate(
        getPaddingLeft() + mTextTranslateOffset.x, getPaddingTop() + mTextTranslateOffset.y);
    drawSelectHandle(canvas);
    canvas.restore();
  }

  private boolean handleTextEditContextTouch(MotionEvent event) {
    TextEditContextSession session = mTextEditContextSession;
    if (session == null || !session.isActive()) {
      return false;
    }
    int action = event.getActionMasked();
    if (action != MotionEvent.ACTION_DOWN && action != MotionEvent.ACTION_MOVE
        && action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
      return false;
    }
    if (mLastEditContextTouchEventTime == event.getEventTime()
        && mLastEditContextTouchAction == action) {
      return true;
    }
    mLastEditContextTouchEventTime = event.getEventTime();
    mLastEditContextTouchAction = action;
    if (action == MotionEvent.ACTION_CANCEL) {
      resetTextEditContextTouchGesture();
      return true;
    }
    TextEditContextSnapshot state = session.snapshot();
    if (state == null) {
      return false;
    }
    int anchor = action == MotionEvent.ACTION_DOWN ? -1 : mEditContextTouchAnchor;
    TextEditContextResult result =
        session.setSelectionFromPoint(this, event.getX(), event.getY(), anchor, state.revision);
    if (result != null && result.accepted && result.snapshot != null) {
      if (action == MotionEvent.ACTION_DOWN) {
        mEditContextTouchAnchor = result.snapshot.selectionBase;
      }
      updateTextEditContextSelection(result.snapshot);
      notifyTextEditContextSelection(result.snapshot);
      invalidate();
    }
    if (action == MotionEvent.ACTION_UP) {
      finishTextEditContextTouchGesture();
    }
    return true;
  }

  private boolean isNearEditContextHandle(PointF handle, float x, float y) {
    return !mEditContextSelectionCollapsed && handle.x >= 0.f && handle.y >= 0.f
        && distanceBetweenPoints(handle, x, y) < RESPONSE_TOUCH_RADIUS;
  }

  private void beginTextEditContextTouchGesture(MotionEvent event) {
    mEditContextIgnoreTouchSequence = false;
    mEditContextLongPressTriggered = false;
    mEditContextTouchDownX = event.getX();
    mEditContextTouchDownY = event.getY();
    mEditContextAdjustingStartHandle =
        isNearEditContextHandle(mEditContextStartHandle, event.getX(), event.getY());
    mEditContextAdjustingEndHandle = !mEditContextAdjustingStartHandle
        && isNearEditContextHandle(mEditContextEndHandle, event.getX(), event.getY());
    if (mEditContextAdjustingStartHandle || mEditContextAdjustingEndHandle) {
      mEditContextTouchAnchor =
          mEditContextAdjustingStartHandle ? mEditContextSelectionEnd : mEditContextSelectionStart;
      requestDisallowInterceptTouchEvent(true);
      return;
    }
    removeTextEditContextLongPressCallback();
    mEditContextLongPressRunnable = () -> {
      if (!mEditContextRootTouchActive || mEditContextIgnoreTouchSequence) {
        return;
      }
      mEditContextLongPressTriggered = selectTextEditContextWordAtCaret();
      if (mEditContextLongPressTriggered) {
        requestDisallowInterceptTouchEvent(true);
      }
    };
    postDelayed(mEditContextLongPressRunnable, ViewConfiguration.getLongPressTimeout());
  }

  private boolean selectTextEditContextWordAtCaret() {
    TextEditContextSession session = mTextEditContextSession;
    TextEditContextSnapshot state = session == null ? null : session.snapshot();
    if (session == null || state == null || state.text.isEmpty() || mEditContextTouchAnchor < 0) {
      return false;
    }
    int index = Math.min(mEditContextTouchAnchor, state.text.length() - 1);
    if (index > 0 && Character.isWhitespace(state.text.charAt(index))
        && !Character.isWhitespace(state.text.charAt(index - 1))) {
      index--;
    }
    int start;
    int end;
    if (state.text.charAt(index) == '\uFFFC') {
      start = index;
      end = index + 1;
    } else {
      BreakIterator iterator = BreakIterator.getWordInstance();
      iterator.setText(state.text);
      start = iterator.preceding(index + 1);
      end = iterator.following(index);
      if (start == BreakIterator.DONE || end == BreakIterator.DONE || start == end) {
        start = index;
        end = Math.min(state.text.length(),
            index + Character.charCount(Character.codePointAt(state.text, index)));
      }
    }
    TextEditContextResult result = session.apply(new TextEditContextSession.Transaction("", false,
        0, 0, "", start, end, state.compositionStart, state.compositionEnd, state.revision));
    if (result == null || !result.accepted || result.snapshot == null) {
      return false;
    }
    mEditContextTouchAnchor = result.snapshot.selectionBase;
    updateTextEditContextSelection(result.snapshot);
    notifyTextEditContextSelection(result.snapshot);
    invalidate();
    return true;
  }

  private void removeTextEditContextLongPressCallback() {
    if (mEditContextLongPressRunnable != null) {
      removeCallbacks(mEditContextLongPressRunnable);
      mEditContextLongPressRunnable = null;
    }
  }

  private void finishTextEditContextTouchGesture() {
    removeTextEditContextLongPressCallback();
    requestDisallowInterceptTouchEvent(false);
    mEditContextTouchAnchor = -1;
    mEditContextAdjustingStartHandle = false;
    mEditContextAdjustingEndHandle = false;
    mEditContextLongPressTriggered = false;
  }

  private void resetTextEditContextTouchGesture() {
    finishTextEditContextTouchGesture();
    mEditContextRootTouchActive = false;
    mEditContextIgnoreTouchSequence = false;
  }

  public boolean hasActiveTextEditContext() {
    return mTextEditContextSession != null && mTextEditContextSession.isActive();
  }

  public boolean dispatchTextEditContextTouchFromRoot(MotionEvent event) {
    if (!hasActiveTextEditContext()) {
      return false;
    }
    int[] screen = new int[2];
    getLocationOnScreen(screen);
    MotionEvent localEvent = MotionEvent.obtain(event);
    localEvent.setLocation(event.getRawX() - screen[0], event.getRawY() - screen[1]);
    try {
      int action = localEvent.getActionMasked();
      if (action == MotionEvent.ACTION_DOWN) {
        mEditContextRootTouchActive = localEvent.getX() >= 0.f && localEvent.getX() < getWidth()
            && localEvent.getY() >= 0.f && localEvent.getY() < getHeight();
        if (mEditContextRootTouchActive) {
          beginTextEditContextTouchGesture(localEvent);
        }
      }
      if (!mEditContextRootTouchActive) {
        return false;
      }
      if (action == MotionEvent.ACTION_MOVE && !mEditContextLongPressTriggered
          && !mEditContextAdjustingStartHandle && !mEditContextAdjustingEndHandle) {
        float deltaX = localEvent.getX() - mEditContextTouchDownX;
        float deltaY = localEvent.getY() - mEditContextTouchDownY;
        int touchSlop = ViewConfiguration.get(getContext()).getScaledTouchSlop();
        if (Math.abs(deltaY) > touchSlop && Math.abs(deltaY) > Math.abs(deltaX)) {
          removeTextEditContextLongPressCallback();
          mEditContextRootTouchActive = false;
          mEditContextIgnoreTouchSequence = true;
          mEditContextTouchAnchor = -1;
          return false;
        }
        if (Math.abs(deltaX) > touchSlop || Math.abs(deltaY) > touchSlop) {
          removeTextEditContextLongPressCallback();
        }
      }
      if (!mEditContextAdjustingStartHandle && !mEditContextAdjustingEndHandle) {
        boolean selectionGestureCaptured = mEditContextLongPressTriggered;
        handleTextEditContextTouch(localEvent);
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
          mEditContextRootTouchActive = false;
        }
        return selectionGestureCaptured;
      }
      TextEditContextSnapshot state = mTextEditContextSession.snapshot();
      if (state == null) {
        return false;
      }
      mLastEditContextTouchEventTime = localEvent.getEventTime();
      mLastEditContextTouchAction = action;
      TextEditContextResult result = mTextEditContextSession.setSelectionFromPoint(
          this, localEvent.getX(), localEvent.getY(), mEditContextTouchAnchor, state.revision);
      if (result != null && result.accepted && result.snapshot != null) {
        updateTextEditContextSelection(result.snapshot);
        notifyTextEditContextSelection(result.snapshot);
        invalidate();
      }
      if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
        resetTextEditContextTouchGesture();
      }
      return true;
    } finally {
      localEvent.recycle();
    }
  }

  public static boolean dispatchTouchToActiveTextEditContext(MotionEvent event) {
    AndroidText host = sActiveTextEditContextHost == null ? null : sActiveTextEditContextHost.get();
    return host != null && host.dispatchTextEditContextTouchFromRoot(event);
  }

  void refreshTextEditContextGeometry() {
    TextEditContextSession session = mTextEditContextSession;
    TextEditContextSnapshot state = session == null ? null : session.snapshot();
    int start = state == null ? -1 : Math.min(state.selectionBase, state.selectionExtent);
    int end = state == null ? -1 : Math.max(state.selectionBase, state.selectionExtent);
    refreshTextEditContextGeometry(start, end);
  }

  void refreshTextEditContextGeometry(int requestedStart, int requestedEnd) {
    TextEditContextSession session = mTextEditContextSession;
    TextEditContextSnapshot state = session == null ? null : session.snapshot();
    if (session == null || state == null || !session.isActive()
        || !session.refreshLayout(this, requestedStart, requestedEnd)) {
      mEditContextSelectionRects.clear();
    } else {
      updateTextEditContextSelection(state);
    }
  }

  void onTextEditContextStateChanged(
      TextEditContextSession source, TextEditContextSnapshot callbackState) {
    TextEditContextSnapshot state;
    if (source == null || source != mTextEditContextSession || callbackState == null
        || (state = source.snapshot()) == null || state.revision < callbackState.revision) {
      return;
    }
    if (mTextEditContextInputConnection != null) {
      mTextEditContextInputConnection.onSessionStateChanged(state);
    } else {
      refreshTextEditContextGeometry();
      notifyTextEditContextSelection(state);
    }
    invalidate();
  }

  private void updateTextEditContextSelection(TextEditContextSnapshot state) {
    if (mTextEditContextSession == null) {
      return;
    }
    float[] rects =
        mTextEditContextSession.selectionRects(state.selectionBase, state.selectionExtent);
    if (rects == null) {
      rects = new float[0];
    }
    int[] screen = new int[2];
    getLocationInWindow(screen);
    mEditContextSelectionRects.clear();
    for (int index = 0; index + 3 < rects.length; index += 4) {
      mEditContextSelectionRects.add(new RectF(rects[index] - screen[0],
          rects[index + 1] - screen[1], rects[index] - screen[0] + rects[index + 2],
          rects[index + 1] - screen[1] + rects[index + 3]));
    }
    mEditContextSelectionCollapsed = state.selectionBase == state.selectionExtent;
    mEditContextSelectionStart = Math.min(state.selectionBase, state.selectionExtent);
    mEditContextSelectionEnd = Math.max(state.selectionBase, state.selectionExtent);
    if (!mEditContextSelectionCollapsed && !mEditContextSelectionRects.isEmpty()) {
      RectF first = mEditContextSelectionRects.get(0);
      RectF last = mEditContextSelectionRects.get(mEditContextSelectionRects.size() - 1);
      mEditContextStartHandle.set(first.left, first.bottom);
      mEditContextEndHandle.set(last.right, last.bottom);
    } else {
      mEditContextStartHandle.set(-1.f, -1.f);
      mEditContextEndHandle.set(-1.f, -1.f);
    }
  }

  private void notifyTextEditContextSelection(TextEditContextSnapshot state) {
    InputMethodManager manager =
        (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
    if (manager != null) {
      manager.updateSelection(this, state.selectionBase, state.selectionExtent,
          state.compositionStart, state.compositionEnd);
    }
  }

  private void drawTextEditContextSelection(Canvas canvas) {
    Paint paint =
        mEditContextSelectionCollapsed ? mEditContextCaretPaint : mEditContextSelectionPaint;
    for (RectF rect : mEditContextSelectionRects) {
      canvas.drawRect(rect, paint);
    }
    if (mEditContextSelectionCollapsed || mEditContextStartHandle.x < 0.f
        || mEditContextEndHandle.x < 0.f) {
      return;
    }
    if (mSelectionLeftCursor == null || mSelectionRightCursor == null) {
      initSelectionCursor(getContext());
    }
    updateSelectionStyle();
    canvas.save();
    canvas.translate(mEditContextStartHandle.x - mSelectionLeftCursor.getBounds().width() / 2.f,
        mEditContextStartHandle.y);
    mSelectionLeftCursor.draw(canvas);
    canvas.restore();
    canvas.save();
    canvas.translate(mEditContextEndHandle.x - mSelectionRightCursor.getBounds().width() / 2.f,
        mEditContextEndHandle.y);
    mSelectionRightCursor.draw(canvas);
    canvas.restore();
  }

  private void drawTextServiceSelectHandle(Canvas canvas) {
    if (!mIsInSelection || mSelectionLeftCursor == null || mSelectionRightCursor == null) {
      return;
    }
    canvas.save();
    canvas.translate(getTextDrawOffsetX(), getTextDrawOffsetY());
    drawSelectHandle(canvas);
    canvas.restore();
  }

  float getTextDrawOffsetX() {
    return getPaddingLeft() + (mTextTranslateOffset != null ? mTextTranslateOffset.x : 0);
  }

  float getTextDrawOffsetY() {
    return getPaddingTop() + (mTextTranslateOffset != null ? mTextTranslateOffset.y : 0);
  }

  private void drawHighlightWithTextOffset(Canvas canvas) {
    canvas.save();
    canvas.translate(getTextDrawOffsetX(), getTextDrawOffsetY());
    drawHighlight(canvas);
    canvas.restore();
  }

  private void drawTextOnCanvas(Canvas canvas) {
    if (mIsJustify && Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
      TextHelper.drawText(canvas, mTextLayout, getWidth() - getPaddingLeft() - getPaddingRight());
    } else {
      mTextLayout.draw(canvas);
    }
  }

  private void drawHighlight(Canvas canvas) {
    if (!mIsInSelection || mHighlightPath == null || mHighlightPaint == null) {
      return;
    }
    int selectStart = Math.min(mSelectStart, mSelectEnd);
    int selectEnd = Math.max(mSelectStart, mSelectEnd);
    if (mTextraPage != null) {
      ArrayList<RectF> boxes = getTextServiceSelectionBoxes(selectStart, selectEnd);
      for (int i = 0; i < boxes.size(); i++) {
        canvas.drawRect(boxes.get(i), mHighlightPaint);
      }
    } else if (mTextLayout != null) {
      mHighlightPath.reset();
      mTextLayout.getSelectionPath(selectStart, selectEnd, mHighlightPath);
      if (!mHighlightPath.isEmpty()) {
        // Using draw (Canvas canvas, Path highlight, Paint highlightPaint,int cursorOffsetVertical)
        // method does not draw highlighting on some Android phones.
        canvas.drawPath(mHighlightPath, mHighlightPaint);
      }
    }
  }

  private void drawText(Canvas canvas) {
    drawHighlight(canvas);
    drawTextOnCanvas(canvas);
    if (mNeedDrawStroke) {
      TextHelper.drawTextStroke(mTextLayout, canvas);
    }
    TextHelper.drawLine(canvas, mTextLayout);
  }

  /**
   * Get bounding box of the specified range of text.
   * @param start start index of text
   * @param end end index of text
   * @return
   */
  public ArrayList<RectF> getTextBoundingBoxes(int start, int end) {
    ArrayList<RectF> boxes = new ArrayList<>();
    if (start > end || start < 0) {
      return boxes;
    }

    if (mTextraPage != null) {
      return new ArrayList<>(getTextServiceSelectionBoxes(start, end));
    }

    if (mTextLayout == null || mTextLayout.getText().length() < end) {
      return boxes;
    }

    if (start == end) {
      int lineIndex = mTextLayout.getLineForOffset(start);
      int lineHeight = mTextLayout.getLineBottom(lineIndex) - mTextLayout.getLineTop(lineIndex);
      boxes.add(new RectF(0, 0, 0, lineHeight));
    } else {
      int startLineIndex = mTextLayout.getLineForOffset(start);
      int endLineIndex = mTextLayout.getLineForOffset(end);
      for (int lineIndex = startLineIndex; lineIndex <= endLineIndex; lineIndex++) {
        Rect lineRect = new Rect();
        mTextLayout.getLineBounds(lineIndex, lineRect);
        if (lineIndex == startLineIndex || lineIndex == endLineIndex) {
          lineRect.left = (int) Math.max(lineRect.left, mTextLayout.getSecondaryHorizontal(start));
          lineRect.right = (int) Math.min(lineRect.right, mTextLayout.getSecondaryHorizontal(end));
        }
        lineRect.left -= mTextTranslateOffset.x;
        lineRect.right -= mTextTranslateOffset.x;
        lineRect.top -= mTextTranslateOffset.y;
        lineRect.bottom -= mTextTranslateOffset.y;
        boxes.add(new RectF(lineRect));
      }
    }

    return boxes;
  }

  public void setBindSelectionChange(boolean isBindSelectionChange, int sign) {
    mIsBindSelectionChange = isBindSelectionChange;
    mSign = sign;
  }

  private void onSelectionChange() {
    if (mIsBindSelectionChange && getContext() instanceof LynxContext) {
      LynxDetailEvent event = new LynxDetailEvent(mSign, SELECTION_CHANGE_EVENT);
      event.addDetail("start", mSelectStart);
      event.addDetail("end", mSelectEnd);
      event.addDetail("direction", mIsForward ? "forward" : "backward");
      ((LynxContext) getContext()).getEventEmitter().sendCustomEvent(event);
    }
  }

  public void setCustomContextMenu(boolean enable) {
    mEnableCustomContextMenu = enable;
  }

  public void setCustomTextSelection(boolean enable) {
    mEnableCustomTextSelection = enable;
  }

  /**
   * Set text selection.
   * @param startX The x-coordinate of the start of the selected text relative to the text component
   * @param startY The y-coordinate of the start of the selected text relative to the text component
   * @param endX The x-coordinate of the end of the selected text relative to the text component
   * @param endY The y-coordinate of the end of the selected text relative to the text component
   * @param showStartHandle Whether to show start handle
   * @param showEndHandle Whether to show end handle
   * @return The bounding boxes of each line
   */
  public ArrayList<RectF> setTextSelection(float startX, float startY, float endX, float endY,
      boolean showStartHandle, boolean showEndHandle) {
    if (mTextraPage != null) {
      return setTextServiceTextSelection(
          startX, startY, endX, endY, showStartHandle, showEndHandle);
    }

    invalidate();
    if (startX < 0 || startY < 0 || endX < 0 || endY < 0) {
      clearSelection();
      return new ArrayList<>();
    }
    int startIndex = getOffsetForPosition(startX, startY);
    int endIndex = getOffsetForPosition(endX, endY);
    if (startIndex < 0 || endIndex < 0) {
      clearSelection();
      return new ArrayList<>();
    }
    if (startIndex == endIndex) {
      PointF point = getCenterPositionForOffset(startIndex);
      if (startIndex == mTextLayout.getText().length() || (startIndex > 0 && startX < point.x)) {
        startIndex--;
      } else {
        endIndex++;
      }
    }

    mIsShowStartHandle = showStartHandle;
    mIsShowEndHandle = showEndHandle;
    mIsInSelection = true;

    updateSelectionRange(startIndex, endIndex);
    updateSelectStartEnd();
    return getTextBoundingBoxes(mSelectStart, mSelectEnd);
  }

  private ArrayList<RectF> setTextServiceTextSelection(float startX, float startY, float endX,
      float endY, boolean showStartHandle, boolean showEndHandle) {
    invalidate();
    if (startX < 0 || startY < 0 || endX < 0 || endY < 0) {
      clearSelection();
      return new ArrayList<>();
    }
    int startIndex = getOffsetForPosition(startX, startY);
    int endIndex = getOffsetForPosition(endX, endY);
    if (startIndex < 0 || endIndex < 0) {
      clearSelection();
      return new ArrayList<>();
    }
    if (startIndex == endIndex) {
      endIndex++;
      if (getTextBoundingBoxes(startIndex, endIndex).isEmpty() && startIndex > 0) {
        startIndex--;
        endIndex--;
      }
    }

    ArrayList<RectF> boxes =
        getTextBoundingBoxes(Math.min(startIndex, endIndex), Math.max(startIndex, endIndex));
    if (boxes.isEmpty()) {
      clearSelection();
      return boxes;
    }

    mIsShowStartHandle = showStartHandle;
    mIsShowEndHandle = showEndHandle;
    mIsInSelection = true;

    updateSelectionRange(startIndex, endIndex);
    updateSelectStartEnd();
    return boxes;
  }

  /**
   * Get position and default response click radius of handles.
   * @return Handles list
   */
  public ArrayList<Float>[] getHandlesInfo() {
    if (!mIsInSelection) {
      return new ArrayList[0];
    }
    ArrayList<Float>[] handlesInfo = new ArrayList[2];
    ArrayList<Float> startHandle = new ArrayList<>();
    startHandle.add(mStartHandlerPos.x);
    startHandle.add(mStartHandlerPos.y);
    startHandle.add(RESPONSE_TOUCH_RADIUS);
    handlesInfo[0] = startHandle;
    ArrayList<Float> endHandle = new ArrayList<>();
    endHandle.add(mEndHandlerPos.x);
    endHandle.add(mEndHandlerPos.y);
    endHandle.add(RESPONSE_TOUCH_RADIUS);
    handlesInfo[1] = endHandle;
    return handlesInfo;
  }

  /**
   * Get selected text content.
   * @return Selected string
   */
  public String getSelectedText() {
    if (mTextraPage != null && mSelectStart >= 0 && mSelectEnd > mSelectStart) {
      return mTextraPage.getSelectedText(mSelectStart, mSelectEnd);
    }
    if (mTextLayout != null && mSelectStart >= 0 && mSelectEnd > 0 && mSelectEnd > mSelectStart
        && mSelectEnd <= mTextLayout.getText().length()) {
      return mTextLayout.getText().subSequence(mSelectStart, mSelectEnd).toString();
    }
    return "";
  }

  private void drawSelectHandle(Canvas canvas) {
    if (mIsShowStartHandle) {
      drawSelectStartCursor(canvas);
    }
    if (mIsShowEndHandle) {
      drawSelectEndCursor(canvas);
    }
  }

  private void drawSelectStartCursor(Canvas canvas) {
    canvas.save();
    canvas.translate(mStartHandlerPos.x - mSelectionLeftCursor.getBounds().width() / 2.f,
        mStartHandlerPos.y - mSelectionLeftCursor.getBounds().height() / 2.f);
    mSelectionLeftCursor.draw(canvas);
    canvas.restore();
  }

  private void drawSelectEndCursor(Canvas canvas) {
    canvas.save();
    canvas.translate(mEndHandlerPos.x - mSelectionRightCursor.getBounds().width() / 2.f,
        mEndHandlerPos.y - mSelectionRightCursor.getBounds().height() / 2.f);
    mSelectionRightCursor.draw(canvas);
    canvas.restore();
  }

  @Keep
  @Override
  protected boolean verifyDrawable(Drawable drawable) {
    if (mHasImage && getText() instanceof Spanned) {
      Spanned text = (Spanned) getText();
      AbsInlineImageSpan[] spans = text.getSpans(0, text.length(), AbsInlineImageSpan.class);
      for (AbsInlineImageSpan span : spans) {
        if (span.getDrawable() == drawable) {
          return true;
        }
      }
    }
    return super.verifyDrawable(drawable);
  }

  @Keep
  @Override
  public void invalidateDrawable(Drawable drawable) {
    if (!UIThreadUtils.isOnUiThread()) {
      // TextLayoutWarmer may invalidate AndroidText
      return;
    }
    if (mTextraPage != null || drawable == null) {
      invalidate();
      return;
    }
    if (mHasImage && getText() instanceof Spanned) {
      Spanned text = (Spanned) getText();
      AbsInlineImageSpan[] spans = text.getSpans(0, text.length(), AbsInlineImageSpan.class);
      for (AbsInlineImageSpan span : spans) {
        if (span.getDrawable() == drawable) {
          invalidate();
          mOverflowPictureDirty = true;
        }
      }
    }
    super.invalidateDrawable(drawable);
  }

  @Keep
  @Override
  public void onDetachedFromWindow() {
    setTextEditContextSession(null);
    super.onDetachedFromWindow();
    dispatchDetachImageSpan();
  }

  private void dispatchDetachImageSpan() {
    if (mHasImage && getText() instanceof Spanned) {
      Spanned text = (Spanned) getText();
      AbsInlineImageSpan[] spans = text.getSpans(0, text.length(), AbsInlineImageSpan.class);
      for (AbsInlineImageSpan span : spans) {
        span.onDetachedFromWindow();
        span.setCallback(null);
      }
    }
  }

  @Keep
  @Override
  public void onStartTemporaryDetach() {
    super.onStartTemporaryDetach();
    if (mHasImage && getText() instanceof Spanned) {
      Spanned text = (Spanned) getText();
      AbsInlineImageSpan[] spans = text.getSpans(0, text.length(), AbsInlineImageSpan.class);
      for (AbsInlineImageSpan span : spans) {
        span.onStartTemporaryDetach();
      }
    }
  }

  @Keep
  @Override
  public void onAttachedToWindow() {
    super.onAttachedToWindow();
    if (mHasImage && getText() instanceof Spanned) {
      Spanned spannable = (Spanned) getText();
      AbsInlineImageSpan.possiblyUpdateInlineImageSpans(spannable, this);
    }
  }

  @Keep
  @Override
  public void onFinishTemporaryDetach() {
    super.onFinishTemporaryDetach();
    if (mHasImage && getText() instanceof Spanned) {
      Spanned text = (Spanned) getText();
      AbsInlineImageSpan[] spans = text.getSpans(0, text.length(), AbsInlineImageSpan.class);
      for (AbsInlineImageSpan span : spans) {
        span.onFinishTemporaryDetach();
      }
    }
  }

  public CharSequence getText() {
    return mTextLayout != null ? mTextLayout.getText() : null;
  }

  public boolean hasTextraPage() {
    return mTextraPage != null;
  }

  @Nullable
  public Layout getTextLayout() {
    return mTextLayout;
  }

  public void setOverflow(int overflow) {
    mOverflow = overflow;
  }

  private void drawOverflowPicture() {
    if (!mOverflowPictureDirty || mTextLayout == null) {
      return;
    }
    if (mOverflowPicture == null) {
      mOverflowPicture = new Picture();
    } else {
      // There are a few picture already recording exceptions, temporary fallback processing.
      mOverflowPicture.endRecording();
    }
    Canvas canvas =
        mOverflowPicture.beginRecording(mTextLayout.getWidth(), mTextLayout.getHeight());
    canvas.save();
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR2) {
      if (getClipBounds() != null) {
        canvas.clipRect(getClipBounds());
      }
    }
    drawTextOnCanvas(canvas);
    if (mNeedDrawStroke) {
      TextHelper.drawTextStroke(mTextLayout, canvas);
    }
    canvas.restore();
    mOverflowPicture.endRecording();

    mOverflowPictureDirty = false;
  }

  public void release() {
    if (mHasImage && getText() instanceof Spanned) {
      Spanned spannable = (Spanned) getText();
      AbsInlineImageSpan.possiblyUpdateInlineImageSpans(spannable, null);
    }
  }

  private void initSelectionCursor(Context context) {
    mSelectionLeftCursor =
        context.getResources().getDrawable(R.drawable.lynx_text_select_handle_left_material);
    mSelectionRightCursor =
        context.getResources().getDrawable(R.drawable.lynx_text_select_handle_right_material);
    mHighlightPaint = new Paint();
    mHighlightPaint.setStyle(Paint.Style.FILL);
    mHighlightPath = new Path();
  }

  private void updateSelectionStyle() {
    mSelectionLeftCursor.setBounds(0, 0, mHandleSize, mHandleSize);
    mSelectionRightCursor.setBounds(0, 0, mHandleSize, mHandleSize);
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
      mSelectionLeftCursor.setTint(mTextSelectionHandleColor);
      mSelectionRightCursor.setTint(mTextSelectionHandleColor);
    }

    mHighlightPaint.setColor(mTextSelectionColor);
  }

  private int getLineAtCoordinate(float y) {
    y = Math.max(0.f, y);
    y = Math.min(getHeight() - 1, y);

    return mTextLayout.getLineForVertical((int) y);
  }

  private int getOffsetAtCoordinate(int line, float x) {
    float originX = x;
    x = Math.max(0.f, x);
    x = Math.min(getWidth() - 1, x);

    int offset = mTextLayout.getOffsetForHorizontal(line, x);
    float offsetX = mTextLayout.getSecondaryHorizontal(offset);
    if (originX > offsetX + (mTextLayout.getLineRight(line) - offsetX) / 2.0) {
      // contain last char
      offset = mTextLayout.getLineEnd(line);
    }

    return offset;
  }

  private int getOffsetForPosition(float x, float y) {
    if (mTextraPage != null) {
      return mTextraPage.getSelectionCharIndex(x, y);
    }

    if (mTextLayout == null) {
      return -1;
    }
    int line = getLineAtCoordinate(y);

    return getOffsetAtCoordinate(line, x);
  }

  private PointF getBottomPositionForOffset(int offset, boolean isStart) {
    float x = mTextLayout.getPrimaryHorizontal(offset);
    int line = mTextLayout.getLineForOffset(offset);
    float y = mTextLayout.getLineBottom(line);
    // If end cursor is at the start of line, move to the end of last line.
    if (offset == mTextLayout.getLineStart(line) && line > 0 && !isStart) {
      x = mTextLayout.getWidth();
      y = mTextLayout.getLineBottom(line - 1);
    }

    return new PointF(x, y);
  }

  private void showToolbar() {
    if (mEnableCustomContextMenu || !hasTextSelectionContent()) {
      return;
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
      startActionMode(this, ActionMode.TYPE_FLOATING);
    } else {
      startActionMode(this);
    }
  }

  private void hideToolbar() {
    if (mEnableCustomContextMenu || mActionMode == null) {
      return;
    }

    mActionMode.finish();

    mActionMode = null;
  }

  private void updateSelectionRange(int selectStart, int selectEnd) {
    // init cursor drawable if needed
    if (mSelectionLeftCursor == null) {
      initSelectionCursor(getContext());
    }
    updateSelectionStyle();

    mIsForward = mLastSelectStart == -1
        ? selectEnd > selectStart
        : (mLastSelectStart < selectStart || mLastSelectEnd < selectEnd);

    mLastSelectStart = mSelectStart;
    mLastSelectEnd = mSelectEnd;
    mSelectStart = selectStart;
    mSelectEnd = selectEnd;

    if (mTextraPage != null) {
      if (mSelectStart >= 0 && mSelectEnd >= 0) {
        updateSelectionHandlePositionFromBoxes(
            Math.min(mSelectStart, mSelectEnd), Math.max(mSelectStart, mSelectEnd));
        clearOtherSelection();
      }
      return;
    }

    if (mSelectStart >= 0 && mSelectStart <= mTextLayout.getText().length() && mSelectEnd >= 0
        && mSelectEnd <= mTextLayout.getText().length()) {
      if (mTextLayout.getText() instanceof Spannable) {
        Selection.setSelection((Spannable) mTextLayout.getText(),
            Math.min(mSelectStart, mSelectEnd), Math.max(mSelectStart, mSelectEnd));
      }
      mSelectStartPos.set(getBottomPositionForOffset(Math.min(mSelectStart, mSelectEnd), true));
      mSelectEndPos.set(getBottomPositionForOffset(Math.max(mSelectStart, mSelectEnd), false));
      mStartHandlerPos.set(mSelectStartPos.x - mSelectionLeftCursor.getBounds().width() / 2.f,
          mSelectStartPos.y + mSelectionLeftCursor.getBounds().height() / 2.f);
      mEndHandlerPos.set(mSelectEndPos.x + mSelectionRightCursor.getBounds().width() / 2.f,
          mSelectEndPos.y + mSelectionRightCursor.getBounds().height() / 2.f);
      clearOtherSelection();
    } else {
      if (mTextLayout.getText() instanceof Spannable) {
        Selection.removeSelection((Spannable) mTextLayout.getText());
      }
    }
  }

  private void updateSelectionHandlePositionFromBoxes(int selectStart, int selectEnd) {
    ArrayList<RectF> boxes = mTextraPage != null
        ? getTextServiceSelectionBoxes(selectStart, selectEnd)
        : getTextBoundingBoxes(selectStart, selectEnd);
    if (boxes.isEmpty()) {
      return;
    }
    RectF startRect = boxes.get(0);
    RectF endRect = boxes.get(boxes.size() - 1);
    mSelectStartPos.set(startRect.left, startRect.bottom);
    mSelectEndPos.set(endRect.right, endRect.bottom);
    mStartHandlerPos.set(mSelectStartPos.x - mSelectionLeftCursor.getBounds().width() / 2.f,
        mSelectStartPos.y + mSelectionLeftCursor.getBounds().height() / 2.f);
    mEndHandlerPos.set(mSelectEndPos.x + mSelectionRightCursor.getBounds().width() / 2.f,
        mSelectEndPos.y + mSelectionRightCursor.getBounds().height() / 2.f);
  }

  /**
   * Clear other AndroidText's selection if this is selected.
   */
  private void clearOtherSelection() {
    if (mEnableCustomTextSelection) {
      return;
    }
    if (sWeakSelectingAndroidText != null) {
      AndroidText selectingText = sWeakSelectingAndroidText.get();
      if (selectingText != null && selectingText != this) {
        selectingText.clearSelection();
        selectingText.invalidate();
      }
    }
    sWeakSelectingAndroidText = new WeakReference<>(this);
  }

  /**
   * Exchange select start index and end index if need.
   */
  private void updateSelectStartEnd() {
    int minIndex = Math.min(mSelectStart, mSelectEnd);
    mSelectEnd = Math.max(mSelectStart, mSelectEnd);
    mSelectStart = minIndex;
    onSelectionChange();

    if (mTextraPage != null) {
      updateSelectionHandlePositionFromBoxes(mSelectStart, mSelectEnd);
      return;
    }

    mSelectStartPos.set(getBottomPositionForOffset(mSelectStart, true));
    mSelectEndPos.set(getBottomPositionForOffset(mSelectEnd, false));
  }

  private void performBeginSelection(float x, float y) {
    hideToolbar();

    if (mIsInSelection) {
      mShouldResponseMove = true;
      if (distanceBetweenPoints(mStartHandlerPos, x, y) < RESPONSE_TOUCH_RADIUS) {
        adjustStartPosition(x, y);
        requestDisallowInterceptTouchEvent(true);
      } else if (distanceBetweenPoints(mEndHandlerPos, x, y) < RESPONSE_TOUCH_RADIUS) {
        adjustEndPosition(x, y);
        requestDisallowInterceptTouchEvent(true);
      } else {
        mShouldResponseMove = false;
      }
    }
    if (!mIsAdjustEndPos && !mIsAdjustStartPos) {
      removeCheckLongPressCallback();
      mCheckForLongPress = new CheckForLongPress(x, y);
      postDelayed(mCheckForLongPress, ViewConfiguration.getLongPressTimeout());
    }
  }

  private void adjustStartPosition(float x, float y) {
    mIsAdjustStartPos = true;
    int selectStart = getOffsetForPosition(x, y);
    if (selectStart < 0) {
      return;
    }

    if (mSelectEnd == selectStart) {
      if ((mTextraPage == null && selectStart == mTextLayout.getText().length())
          || (x < mSelectEndPos.x && selectStart > 0)) {
        selectStart--;
      } else {
        selectStart++;
      }
    }

    updateSelectionRange(selectStart, mSelectEnd);
  }

  private void adjustEndPosition(float x, float y) {
    mIsAdjustEndPos = true;
    int selectEnd = getOffsetForPosition(x, y);
    if (selectEnd < 0) {
      return;
    }

    if (selectEnd == mSelectStart) {
      if ((mTextraPage == null && selectEnd == mTextLayout.getText().length())
          || (x < mSelectStartPos.x && selectEnd > 0)) {
        selectEnd--;
      } else {
        selectEnd++;
      }
    }

    updateSelectionRange(mSelectStart, selectEnd);
  }

  private PointF getCenterPositionForOffset(int index) {
    if (index < 0 || index > mTextLayout.getText().length()) {
      return new PointF(0, 0);
    }
    int lineIndex = mTextLayout.getLineForOffset(index);
    return new PointF(mTextLayout.getPrimaryHorizontal(index),
        (mTextLayout.getLineTop(lineIndex) + mTextLayout.getLineBottom(lineIndex)) / 2.f);
  }

  private void performMovingSelection(float x, float y) {
    if (mCheckForLongPress != null) {
      if (Math.abs(x - mCheckForLongPress.mX) > 1.f || Math.abs(y - mCheckForLongPress.mY) > 1.f) {
        // touch move before long-press timeout
        removeCheckLongPressCallback();
      }
    }

    if (mIsAdjustStartPos) {
      adjustStartPosition(x, y);
    } else if (mIsAdjustEndPos) {
      adjustEndPosition(x, y);
    }
  }

  private void performEndSelection(float x, float y) {
    requestDisallowInterceptTouchEvent(false);
    if (!mIsInSelection) {
      // touch move before long-press timeout
      removeCheckLongPressCallback();
      return;
    } else {
      if (!mIsAdjustEndPos && !mIsAdjustStartPos) {
        clearSelection();
        return;
      }
    }

    if (mIsAdjustStartPos) {
      adjustStartPosition(x, y);
      updateSelectStartEnd();
    } else if (mIsAdjustEndPos) {
      adjustEndPosition(x, y);
      updateSelectStartEnd();
    }

    showToolbar();
    mIsAdjustEndPos = mIsAdjustStartPos = false;
  }

  /**
   * Clear selection and reset status.
   */
  private void clearSelection() {
    removeCheckLongPressCallback();

    mIsAdjustStartPos = false;
    mIsAdjustEndPos = false;
    mSelectStartPos.set(-1.f, -1.f);
    mSelectEndPos.set(-1.f, -1.f);
    mSelectStart = -1;
    mSelectEnd = -1;
    mLastSelectStart = -1;
    mLastSelectEnd = -1;
    invalidateTextServiceSelectionBoxes();
    if (mIsInSelection) {
      onSelectionChange();
    }
    mIsInSelection = false;
    updateSelectionRange(mSelectStart, mSelectEnd);
    hideToolbar();
    mShouldResponseMove = false;
    mIsShowStartHandle = mIsShowEndHandle = true;
    if (mHighlightPath != null) {
      mHighlightPath.reset();
    }
  }

  private double distanceBetweenPoints(PointF point, float x, float y) {
    return Math.sqrt(Math.pow(point.x - x, 2) + Math.pow(point.y - y, 2));
  }

  private void removeCheckLongPressCallback() {
    if (mCheckForLongPress == null) {
      return;
    }

    removeCallbacks(mCheckForLongPress);
    mCheckForLongPress = null;
  }

  private void performCopy() {
    if (mTextraPage != null) {
      String selectedText = getSelectedText();
      if (!selectedText.isEmpty()) {
        copyToClipboard(selectedText);
      }
    } else if (mSelectStart >= 0 && mSelectEnd > mSelectStart
        && mSelectEnd <= mTextLayout.getText().length()) {
      CharSequence selectedText = mTextLayout.getText().subSequence(mSelectStart, mSelectEnd);
      copyToClipboard(selectedText);
    }

    clearSelection();
  }

  private void copyToClipboard(CharSequence selectedText) {
    ClipData clipped = ClipData.newPlainText("Lynx-clipboard", selectedText);

    ILynxSystemInvokeService systemInvokeService =
        LynxServiceCenter.inst().getService(ILynxSystemInvokeService.class);
    if (systemInvokeService != null) {
      try {
        systemInvokeService.setPrimaryClip(clipped);
      } catch (RemoteException e) {
        LLog.e("AndroidText",
            "A RemoteException was encountered while calling systemInvokeService. "
                + e.getMessage());
      }
    } else {
      ClipboardManager clipManager;
      if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
        clipManager = (ClipboardManager) getContext().getSystemService(ClipboardManager.class);
      } else {
        clipManager = (ClipboardManager) getContext().getSystemService(Context.CLIPBOARD_SERVICE);
      }

      clipManager.setPrimaryClip(clipped);
    }
  }

  private void performSelectAll() {
    if (mTextraPage != null) {
      if (mTextServiceTextLength <= 0) {
        return;
      }
      updateSelectionRange(0, mTextServiceTextLength);
      updateSelectStartEnd();
      return;
    }
    updateSelectionRange(0, mTextLayout.getText().length());
    updateSelectStartEnd();
  }

  private boolean hasTextSelectionContent() {
    return mTextLayout != null || (mTextraPage != null && mTextServiceTextLength > 0);
  }

  private ArrayList<RectF> getTextServiceSelectionBoxes(int start, int end) {
    if (start > end || start < 0 || mTextraPage == null) {
      invalidateTextServiceSelectionBoxes();
      return mTextServiceSelectionBoxes;
    }
    if (mTextServiceSelectionStart == start && mTextServiceSelectionEnd == end) {
      return mTextServiceSelectionBoxes;
    }
    mTextServiceSelectionBoxes.clear();
    mTextServiceSelectionStart = start;
    mTextServiceSelectionEnd = end;
    float[] rects = mTextraPage.getSelectionRects(start, end);
    if (rects == null || rects.length % 4 != 0) {
      return mTextServiceSelectionBoxes;
    }
    for (int i = 0; i < rects.length; i += 4) {
      mTextServiceSelectionBoxes.add(
          new RectF(rects[i], rects[i + 1], rects[i] + rects[i + 2], rects[i + 1] + rects[i + 3]));
    }
    return mTextServiceSelectionBoxes;
  }

  private void invalidateTextServiceSelectionBoxes() {
    mTextServiceSelectionBoxes.clear();
    mTextServiceSelectionStart = -1;
    mTextServiceSelectionEnd = -1;
  }

  @Override
  public boolean onCreateActionMode(ActionMode mode, Menu menu) {
    menu.add(Menu.NONE, ID_COPY, 0, R.string.copy);
    menu.add(Menu.NONE, ID_SELECT_ALL, 1, R.string.selectAll);

    return true;
  }

  @Override
  public boolean onPrepareActionMode(ActionMode mode, Menu menu) {
    mActionMode = mode;
    return false;
  }

  @Override
  public boolean onActionItemClicked(ActionMode mode, MenuItem item) {
    if (item.getItemId() == ID_COPY) {
      performCopy();
    } else if (item.getItemId() == ID_SELECT_ALL) {
      performSelectAll();
    }
    invalidate();
    return true;
  }

  @Override
  public void onDestroyActionMode(ActionMode mode) {}
}
