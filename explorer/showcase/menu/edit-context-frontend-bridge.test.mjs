// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import test from 'node:test';

const require = createRequire(import.meta.url);
const reactPackage = require.resolve('@lynx-js/react/package.json');
const elementModule = path.join(
  path.dirname(reactPackage),
  'worklet-runtime/lib/api/element.js'
);
const { Element } = await import(pathToFileURL(elementModule));

test('main-thread Element forwards the EditContext lifecycle', () => {
  const contexts = new Map();
  const calls = [];
  const rawElement = { uid: 42 };
  const element = new Element(rawElement);

  globalThis.__GetElementUniqueID = (raw) => raw.uid;
  globalThis.__GetEditContextForElement = (hostId) =>
    contexts.get(hostId) ?? null;

  const createContext = (name) => ({
    __attachElement(hostId, wrapper) {
      calls.push([name, 'attach', hostId, wrapper]);
      contexts.set(hostId, this);
    },
    __detachElement(hostId) {
      calls.push([name, 'detach', hostId]);
      contexts.delete(hostId);
    },
    __focus() {
      calls.push([name, 'focus']);
    },
    __blur() {
      calls.push([name, 'blur']);
    },
  });

  const first = createContext('first');
  const second = createContext('second');
  element.editContext = first;
  assert.equal(element.editContext, first);
  element.focus();
  element.blur();

  element.editContext = second;
  assert.equal(element.editContext, second);
  assert.equal(
    calls.some(
      ([name, operation]) => name === 'first' && operation === 'detach'
    ),
    false,
    'replacement must be atomic in the native registry'
  );

  element.editContext = null;
  assert.equal(element.editContext, null);
  assert.throws(() => {
    element.editContext = {};
  }, /editContext must be an EditContext or null/);

  assert.deepEqual(
    calls.map(([name, operation, hostId]) => [name, operation, hostId]),
    [
      ['first', 'attach', 42],
      ['first', 'focus', undefined],
      ['first', 'blur', undefined],
      ['second', 'attach', 42],
      ['second', 'detach', 42],
    ]
  );
});
