// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import { describe, expectTypeOf, it } from 'vitest';
import { MainThread } from '../../types';

describe('Main Thread EditContext interface', () => {
  it('constructs and exposes UTF-16 text state', () => {
    const context = new EditContext({
      text: 'hello',
      selectionStart: 5,
      selectionEnd: 5,
    });
    expectTypeOf(context).toEqualTypeOf<MainThread.EditContext>();
    expectTypeOf(context.text).toBeString();
    expectTypeOf(context.selectionStart).toBeNumber();
    expectTypeOf(context.attachedElements).toEqualTypeOf<MainThread.Element[]>();
  });

  it('types textupdate and programmatic mutations', () => {
    const context = {} as MainThread.EditContext;
    context.updateText(0, 0, 'lynx');
    context.updateSelection(4, 4);
    context.addEventListener('textupdate', (event) => {
      expectTypeOf(event).toEqualTypeOf<MainThread.EditContextTextUpdateEvent>();
      expectTypeOf(event.updateRangeStart).toBeNumber();
      expectTypeOf(event.text).toBeString();
    });
  });

  it('associates with a main-thread Element', () => {
    const element = {} as MainThread.Element;
    const context = {} as MainThread.EditContext;
    element.editContext = context;
    element.focus();
    element.blur();
    expectTypeOf<MainThread.Element['editContext']>().toEqualTypeOf<MainThread.EditContext | null>();
  });
});
