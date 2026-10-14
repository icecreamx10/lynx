// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
// Window-side API. Never executes render/native-tree operations on Window when
// the renderer belongs to a worker. CDP bypasses that queue so resume still
// reaches the existing Inspector while the render thread is paused.
function createLynxBrowserClient(module, mode, onState) {
  const pending = new Map(), resourceRequests = new Map();
  module.lynxResources = createLynxWebResourceProvider({
    provider: window.lynxResourceProvider, baseURL: document.baseURI
  });
  module.lynxFontResources = window.lynxFontResources || {};
  function resourceReply(id, response, error = '') {
    const bytes = response?.data || new Uint8Array();
    const pointer = bytes.length ? module._malloc(bytes.length) : 0;
    try {
      if (bytes.length && !pointer) throw Error('WASM allocation failed');
      if (bytes.length) module.HEAPU8.set(bytes, pointer);
      module.ccall('host_resource_response', null,
        ['number', 'number', 'number', 'string', 'string', 'string'],
        [id, pointer, bytes.length, response?.mime || '', response?.url || '', error]);
    } finally { if (pointer) module._free(pointer); }
  }
  let next = 1, closed = false, initialized = false;
  let readyResolve, readyReject;
  const ready = new Promise((resolve, reject) => {
    readyResolve = resolve;
    readyReject = reject;
  });
  const client = {
    mode,
    ready,
    lastState: null,
    onLynxCDP: null,
    call(method, args = []) {
      if (!initialized) return ready.then(() => client.call(method, args));
      if (closed) return Promise.reject(Error('Lynx host is closed'));
      const direct = {
        devtool_attach: ['number', []],
        devtool_detach: [null, ['number']],
        devtool_dispatch: [null, ['number', 'string']]
      };
      if (direct[method]) {
        const [result, types] = direct[method];
        return Promise.resolve(module.ccall(method, result, types, args));
      }
      if (pending.size >= 128)
        return Promise.reject(Error('Lynx host request queue is full'));
      const id = next++;
      return new Promise((resolve, reject) => {
        pending.set(id, {resolve, reject});
        if (module.ccall(
                'host_request', 'number', ['number', 'string', 'string'],
                [id, method, JSON.stringify(args)])) {
          pending.delete(id);
          reject(Error('Render thread is not ready'));
        }
      });
    },
    async loadBundle(bytes) {
      const pointer = module._malloc(bytes.length);
      if (!pointer) throw Error('WASM allocation failed');
      try {
        module.HEAPU8.set(bytes, pointer);
        return await client.call('load_bundle', [pointer, bytes.length]);
      } finally {
        module._free(pointer);
      }
    },
    close() {
      closed = true;
      for (const handle of resourceRequests.values()) handle.cancel();
      resourceRequests.clear();
      // Already-posted requests retain their buffers until the owner replies.
      // Rejecting a queued load here would free its WASM input too early.
    }
  };
  module.onLynxHost = (kind, id, raw) => {
    const data = JSON.parse(raw);
    if (kind === 'resource') {
      const handle = module.lynxResources.load(data);
      resourceRequests.set(id, handle);
      handle.promise.then(response => {
        if (resourceRequests.get(id) === handle) resourceReply(id, response);
      }).catch(error => {
        if (resourceRequests.get(id) === handle) resourceReply(id, null, String(error.message || error));
      }).finally(() => {
        if (resourceRequests.get(id) === handle) resourceRequests.delete(id);
      });
    } else if (kind === 'resource-cancel') {
      resourceRequests.get(id)?.cancel();
      resourceRequests.delete(id);
    } else if (kind === 'reply') {
      const request = pending.get(id);
      if (!request) return;
      pending.delete(id);
      if (data.ok)
        request.resolve(data.result);
      else
        request.reject(Error(data.error));
    } else if (kind === 'ready') {
      if (!data.ok) {
        readyReject(Error(data.error));
        return;
      }
      initialized = true;
      client.lastState = data;
      onState(data);
      readyResolve(data);
    } else if (kind === 'state') {
      if (data.error)
        onState(data);
      else {
        client.lastState = data;
        onState(data);
      }
    }
  };
  module.onLynxCDP = (...args) => client.onLynxCDP?.(...args);
  const code = module.ccall(
      'host_start', 'number', ['number', 'string', 'string'],
      [mode === 'worker' ? 1 : 0, document.baseURI, JSON.stringify(module.lynxFontResources)]);
  if (code) readyReject(Error('Could not start render thread: ' + code));
  return client;
}
