// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import type { Element } from './element';

export interface EditContextInit {
  text?: string;
  selectionStart?: number;
  selectionEnd?: number;
}

export interface EditContextEvent {
  readonly type:
    | 'textupdate'
    | 'textformatupdate'
    | 'characterboundsupdate'
    | 'compositionstart'
    | 'compositionend';
}

export interface EditContextTextUpdateEvent extends EditContextEvent {
  readonly type: 'textupdate';
  readonly updateRangeStart: number;
  readonly updateRangeEnd: number;
  readonly text: string;
  readonly selectionStart: number;
  readonly selectionEnd: number;
}

export interface EditContextEventMap {
  textupdate: EditContextTextUpdateEvent;
  textformatupdate: EditContextEvent;
  characterboundsupdate: EditContextEvent;
  compositionstart: EditContextEvent;
  compositionend: EditContextEvent;
}

export interface EditContext {
  readonly text: string;
  readonly selectionStart: number;
  readonly selectionEnd: number;
  readonly attachedElements: Element[];

  updateText(rangeStart: number, rangeEnd: number, text: string): void;
  updateSelection(selectionStart: number, selectionEnd: number): void;
  addEventListener<Type extends keyof EditContextEventMap>(
    type: Type,
    listener: (event: EditContextEventMap[Type]) => void,
  ): void;
  removeEventListener<Type extends keyof EditContextEventMap>(
    type: Type,
    listener: (event: EditContextEventMap[Type]) => void,
  ): void;
}

export interface EditContextConstructor {
  new (options?: EditContextInit): EditContext;
}

declare global {
  var EditContext: EditContextConstructor;
}
