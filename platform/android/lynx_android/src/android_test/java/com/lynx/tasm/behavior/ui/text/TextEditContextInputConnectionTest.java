// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

package com.lynx.tasm.behavior.ui.text;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.view.View;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class TextEditContextInputConnectionTest {
  private RecordingSession mSession;
  private TextEditContextInputConnection mConnection;

  @Before
  public void setUp() {
    Context context = ApplicationProvider.getApplicationContext();
    mSession = new RecordingSession(
        new TextEditContextSnapshot("ab\uD83D\uDE00cd", 5, 1, -1, -1, 7));
    mConnection = new TextEditContextInputConnection(new View(context), mSession);
  }

  @Test
  public void synchronousQueriesOrderBackwardSelectionWithoutLosingDirection() {
    assertEquals("b\uD83D\uDE00c", mConnection.getSelectedText(0).toString());
    assertEquals("a", mConnection.getTextBeforeCursor(10, 0).toString());
    assertEquals("d", mConnection.getTextAfterCursor(10, 0).toString());
    assertEquals(5, mConnection.getExtractedText(null, 0).selectionStart);
    assertEquals(1, mConnection.getExtractedText(null, 0).selectionEnd);
  }

  @Test
  public void composingTextReplacesOrderedSelectionAndPublishesComposition() {
    assertTrue(mConnection.setComposingText("xy", 1));
    TextEditContextSession.Transaction transaction = mSession.lastTransaction;
    assertNotNull(transaction);
    assertEquals("insertCompositionText", transaction.inputType);
    assertEquals(1, transaction.rangeStart);
    assertEquals(5, transaction.rangeEnd);
    assertEquals(3, transaction.selectionBase);
    assertEquals(1, transaction.compositionStart);
    assertEquals(3, transaction.compositionEnd);
  }

  @Test
  public void codePointDeletionDoesNotSplitSurrogatePair() {
    mSession.snapshot = new TextEditContextSnapshot("a\uD83D\uDE00b", 3, 3, -1, -1, 9);
    assertTrue(mConnection.deleteSurroundingTextInCodePoints(1, 0));
    TextEditContextSession.Transaction transaction = mSession.lastTransaction;
    assertNotNull(transaction);
    assertEquals("deleteContentBackward", transaction.inputType);
    assertEquals(1, transaction.rangeStart);
    assertEquals(3, transaction.rangeEnd);
  }

  @Test
  public void deletionAddressesAtomicProjectionSlotAsOneCodeUnit() {
    mSession.snapshot = new TextEditContextSnapshot("a\uFFFCb", 2, 2, -1, -1, 10);
    assertTrue(mConnection.deleteSurroundingText(1, 0));
    TextEditContextSession.Transaction transaction = mSession.lastTransaction;
    assertEquals("deleteContentBackward", transaction.inputType);
    assertEquals(1, transaction.rangeStart);
    assertEquals(2, transaction.rangeEnd);
  }

  @Test
  public void zeroCursorUpdateModeDisablesMonitoringSuccessfully() {
    assertTrue(mConnection.requestCursorUpdates(0));
  }

  @Test
  public void selectionDirectionIsForwardedToSharedSession() {
    assertTrue(mConnection.setSelection(5, 1));
    assertEquals(5, mSession.lastTransaction.selectionBase);
    assertEquals(1, mSession.lastTransaction.selectionExtent);
  }

  private static final class RecordingSession implements TextEditContextSession {
    TextEditContextSnapshot snapshot;
    Transaction lastTransaction;

    RecordingSession(TextEditContextSnapshot snapshot) {
      this.snapshot = snapshot;
    }

    @Override
    public boolean activate() {
      return true;
    }

    @Override
    public TextEditContextResult apply(Transaction transaction) {
      lastTransaction = transaction;
      return new TextEditContextResult(TextEditContextResult.STATUS_ACCEPTED, false, snapshot);
    }

    @Override
    public void deactivate() {}

    @Override
    public boolean isActive() {
      return true;
    }

    @Override
    public TextEditContextResult performInput(
        String inputType, String data, long expectedRevision) {
      return new TextEditContextResult(TextEditContextResult.STATUS_ACCEPTED, false, snapshot);
    }

    @Override
    public TextEditContextSnapshot snapshot() {
      return snapshot;
    }
  }
}
