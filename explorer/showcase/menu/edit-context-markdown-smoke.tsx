// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import {
  root,
  runOnBackground,
  useMainThreadRef,
  useState,
} from '@lynx-js/react';

const OBJECT_REPLACEMENT = '\uFFFC';

type TextStyle = 'heading' | 'plain' | 'marked' | 'code';
type DocumentNode =
  | { type: 'text'; style: TextStyle; text: string }
  | { type: 'atom'; atom: 'mention' | 'diagram'; block: boolean };

type TextUpdate = {
  updateRangeStart: number;
  updateRangeEnd: number;
  text: string;
  selectionStart: number;
  selectionEnd: number;
  contextText: string;
};

type SmokeStatus = {
  phase: string;
  textUpdates: number;
  beforeInputs: number;
  selectionChanges: number;
  compositionDepth: number;
  selectionStart: number;
  selectionEnd: number;
  textLength: number;
  lastInput: string;
  lastRange: string;
  frontendProjection: string;
  projectionMatchesContext: boolean;
};

const INITIAL_DOCUMENT: DocumentNode[] = [
  { type: 'text', style: 'heading', text: '# EditContext foundation\n' },
  { type: 'text', style: 'plain', text: 'Select across ' },
  { type: 'atom', atom: 'mention', block: false },
  { type: 'text', style: 'plain', text: ' and ' },
  { type: 'text', style: 'marked', text: 'keep typing' },
  {
    type: 'text',
    style: 'code',
    text: '.\n```js const answer = 42;```\n',
  },
  { type: 'atom', atom: 'diagram', block: true },
  {
    type: 'text',
    style: 'plain',
    text: '\nContinue editing after the block atom.',
  },
];

function nodeLength(node: DocumentNode): number {
  return node.type === 'text' ? node.text.length : 1;
}

function projectionOf(nodes: DocumentNode[]): string {
  return nodes
    .map((node) => (node.type === 'text' ? node.text : OBJECT_REPLACEMENT))
    .join('');
}

function insertionStyle(nodes: DocumentNode[], offset: number): TextStyle {
  let position = 0;
  let previous: TextStyle = 'plain';
  for (const node of nodes) {
    const end = position + nodeLength(node);
    if (node.type === 'text') {
      if (offset >= position && offset <= end) {
        return node.style;
      }
      previous = node.style;
    } else if (offset < end) {
      return previous;
    }
    position = end;
  }
  return previous;
}

function appendNode(result: DocumentNode[], node: DocumentNode): void {
  if (node.type === 'text' && node.text.length === 0) {
    return;
  }
  const previous = result[result.length - 1];
  if (
    previous?.type === 'text' &&
    node.type === 'text' &&
    previous.style === node.style
  ) {
    previous.text += node.text;
    return;
  }
  result.push(node);
}

// EditContext offsets and JavaScript string slices are both UTF-16 code units.
// Atomic descendants occupy one U+FFFC code unit in the logical projection.
function applyTextUpdate(
  nodes: DocumentNode[],
  update: TextUpdate
): DocumentNode[] {
  const documentLength = projectionOf(nodes).length;
  const start = Math.max(0, Math.min(update.updateRangeStart, documentLength));
  const end = Math.max(start, Math.min(update.updateRangeEnd, documentLength));
  const style = insertionStyle(nodes, start);
  const result: DocumentNode[] = [];
  let position = 0;
  let inserted = false;

  const insertReplacement = () => {
    if (!inserted) {
      appendNode(result, { type: 'text', style, text: update.text });
      inserted = true;
    }
  };

  for (const node of nodes) {
    const length = nodeLength(node);
    const nodeStart = position;
    const nodeEnd = position + length;

    if (nodeEnd <= start) {
      appendNode(result, { ...node });
    } else if (nodeStart >= end) {
      insertReplacement();
      appendNode(result, { ...node });
    } else if (node.type === 'text') {
      const keptPrefix = node.text.slice(0, Math.max(0, start - nodeStart));
      const keptSuffix = node.text.slice(Math.max(0, end - nodeStart));
      appendNode(result, { ...node, text: keptPrefix });
      insertReplacement();
      appendNode(result, { ...node, text: keptSuffix });
    } else {
      // A selected atom is deleted as one logical U+FFFC code unit.
      insertReplacement();
    }
    position = nodeEnd;
  }
  insertReplacement();
  return result;
}

const INITIAL_PROJECTION = projectionOf(INITIAL_DOCUMENT);
const INITIAL_CARET = 56;
const INITIAL_STATUS: SmokeStatus = {
  phase: 'READY — tap the editor, then type',
  textUpdates: 0,
  beforeInputs: 0,
  selectionChanges: 0,
  compositionDepth: 0,
  selectionStart: INITIAL_CARET,
  selectionEnd: INITIAL_CARET,
  textLength: INITIAL_PROJECTION.length,
  lastInput: 'none',
  lastRange: 'none',
  frontendProjection: INITIAL_PROJECTION,
  projectionMatchesContext: true,
};

function App() {
  const [document, setDocument] = useState<DocumentNode[]>(INITIAL_DOCUMENT);
  const [status, setStatus] = useState<SmokeStatus>(INITIAL_STATUS);
  const contextRef = useMainThreadRef<any>(null);

  function applyFrontendTextUpdate(update: TextUpdate) {
    setDocument((previous) => applyTextUpdate(previous, update));
    setStatus((previous) => ({
      ...previous,
      phase: 'ACTIVE — native textupdate projected by React',
      textUpdates: previous.textUpdates + 1,
      selectionStart: update.selectionStart,
      selectionEnd: update.selectionEnd,
      textLength: update.contextText.length,
      lastInput: update.text.length === 0 ? 'delete' : update.text,
      lastRange: `${update.updateRangeStart}:${update.updateRangeEnd}`,
      frontendProjection:
        previous.frontendProjection.slice(0, update.updateRangeStart) +
        update.text +
        previous.frontendProjection.slice(update.updateRangeEnd),
      projectionMatchesContext:
        previous.frontendProjection.slice(0, update.updateRangeStart) +
          update.text +
          previous.frontendProjection.slice(update.updateRangeEnd) ===
        update.contextText,
    }));
  }

  function reportActivation(phase: string, textLength: number) {
    setStatus((previous) => ({ ...previous, phase, textLength }));
  }

  function reportBeforeInput(inputType: string, data: string) {
    setStatus((previous) => ({
      ...previous,
      beforeInputs: previous.beforeInputs + 1,
      lastInput: `${inputType}:${data || '∅'}`,
    }));
  }

  function reportSelection(selectionStart: number, selectionEnd: number) {
    setStatus((previous) => ({
      ...previous,
      selectionChanges: previous.selectionChanges + 1,
      selectionStart,
      selectionEnd,
    }));
  }

  function reportComposition(delta: number) {
    setStatus((previous) => ({
      ...previous,
      compositionDepth: Math.max(0, previous.compositionDepth + delta),
    }));
  }

  function onTextUpdate(event: TextUpdate) {
    'main thread';
    const context = contextRef.current;
    runOnBackground(applyFrontendTextUpdate)({
      updateRangeStart: event.updateRangeStart,
      updateRangeEnd: event.updateRangeEnd,
      text: event.text,
      selectionStart: event.selectionStart,
      selectionEnd: event.selectionEnd,
      contextText: context ? String(context.text) : '',
    });
  }

  function onCompositionStart() {
    'main thread';
    runOnBackground(reportComposition)(1);
  }

  function onCompositionEnd() {
    'main thread';
    runOnBackground(reportComposition)(-1);
  }

  function activate(event: any) {
    'main thread';
    const host = event.currentTarget;
    let step = 'read context ref';
    try {
      let context = contextRef.current;
      if (!context) {
        step = 'resolve EditContext constructor';
        const EditContextConstructor = (globalThis as any).EditContext;
        if (typeof EditContextConstructor !== 'function') {
          runOnBackground(reportActivation)(
            'FAILED — globalThis.EditContext is unavailable',
            0
          );
          return;
        }
        step = 'construct EditContext';
        context = new EditContextConstructor({
          text: INITIAL_PROJECTION,
          selectionStart: INITIAL_CARET,
          selectionEnd: INITIAL_CARET,
        });
        step = `register textupdate (${typeof context.addEventListener})`;
        context.addEventListener('textupdate', onTextUpdate);
        step = 'register compositionstart';
        context.addEventListener('compositionstart', onCompositionStart);
        step = 'register compositionend';
        context.addEventListener('compositionend', onCompositionEnd);
        step = 'assign element.editContext';
        host.editContext = context;
        step = 'persist context ref';
        contextRef.current = context;
      }
      step = `focus element (${typeof host.focus})`;
      host.focus();
      runOnBackground(reportActivation)(
        'FOCUSED — EditContext attached, keyboard input is live',
        context.text.length
      );
    } catch (error) {
      runOnBackground(reportActivation)(
        `FAILED at ${step} — ${String(error)}`,
        0
      );
    }
  }

  function onBeforeInput(event: any) {
    'main thread';
    const detail = event.detail || {};
    runOnBackground(reportBeforeInput)(
      String(detail.inputType || 'unknown'),
      String(detail.data || '')
    );
  }

  function onSelectionChange(event: any) {
    'main thread';
    const detail = event.detail || {};
    runOnBackground(reportSelection)(
      Number(detail.selectionStart || 0),
      Number(detail.selectionEnd || 0)
    );
  }

  return (
    <scroll-view
      scroll-orientation="vertical"
      style={{ width: '100%', height: '100%', backgroundColor: '#f2f4f8' }}
    >
      <view style={{ padding: '28px' }}>
        <text style={{ color: '#5b67f1', fontSize: '13px', fontWeight: '700' }}>
          W3C EDITCONTEXT FOUNDATION
        </text>
        <text
          style={{
            color: '#151825',
            fontSize: '30px',
            fontWeight: '700',
            marginTop: '8px',
          }}
        >
          Frontend-owned Markdown editor
        </text>
        <text style={{ color: '#646b7a', fontSize: '15px', marginTop: '8px' }}>
          One UTF-16 projection; React keeps rich runs and atoms, native owns
          input state and events.
        </text>

        <view
          style={{
            marginTop: '20px',
            padding: '20px',
            borderRadius: '12px',
            backgroundColor: '#ffffff',
          }}
        >
          <text
            id="edit-context-status"
            style={{ color: '#16825d', fontSize: '14px' }}
          >
            {status.phase}
          </text>
          <text
            id="edit-context-counters"
            style={{ color: '#5d6472', fontSize: '12px', marginTop: '6px' }}
          >
            {`projection=${status.projectionMatchesContext ? 'PASS' : 'FAIL'} textupdate=${status.textUpdates} beforeinput=${status.beforeInputs} selectionchange=${status.selectionChanges} composition=${status.compositionDepth} selection=${status.selectionStart}:${status.selectionEnd} length=${status.textLength} range=${status.lastRange} last=${status.lastInput}`}
          </text>

          <text
            id="editing-host"
            {...({ flatten: 'false' } as any)}
            main-thread:bindtap={activate}
            main-thread:bindmousedown={activate}
            main-thread:bindbeforeinput={onBeforeInput}
            main-thread:bindselectionchange={onSelectionChange}
            style={{
              marginTop: '16px',
              minHeight: '260px',
              padding: '18px',
              border: '2px solid #c8cdfd',
              backgroundColor: '#fbfbfe',
              color: '#242735',
              fontSize: '18px',
              lineHeight: '32px',
              whiteSpace: 'pre-wrap',
            }}
          >
            {projectionOf(document)}
          </text>
        </view>
      </view>
    </scroll-view>
  );
}

root.render(<App />);
