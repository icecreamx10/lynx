// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
// Hosts may supply an async provider(request) returning Response, Blob,
// ArrayBuffer, Uint8Array, or {data, mime, url}. Decode stays with consumers.
globalThis.createLynxWebResourceProvider = function(options = {}) {
  const provider = options.provider;
  const baseURL = options.baseURL || document.baseURI;
  const timeout = options.timeout ?? 30000;
  const active = new Map(), history = [];
  let sequence = 0;
  const abortError = () => new DOMException('Resource request cancelled', 'AbortError');
  async function bytes(result, maxBytes, signal) {
    let mime = '', url = '';
    if (result instanceof Response) {
      if (!result.ok) {
        await result.body?.cancel();
        throw new Error(`Resource HTTP ${result.status}`);
      }
      mime = result.headers.get('content-type') || '';
      url = result.url;
      if (Number(result.headers.get('content-length')) > maxBytes) {
        await result.body?.cancel();
        throw new Error('Resource exceeds size limit');
      }
      if (result.body) {
        const reader = result.body.getReader(), chunks = [];
        let size = 0;
        const cancel = () => { void reader.cancel().catch(() => {}); };
        signal.addEventListener('abort', cancel, {once:true});
        try {
          while (true) {
            if (signal.aborted) throw abortError();
            const {done, value} = await reader.read();
            if (done) break;
            size += value.byteLength;
            if (size > maxBytes) throw new Error('Resource exceeds size limit');
            chunks.push(value);
          }
        } catch (error) { await reader.cancel().catch(() => {}); throw error; }
        finally { signal.removeEventListener('abort', cancel); reader.releaseLock(); }
        const data = new Uint8Array(size);
        let offset = 0;
        for (const chunk of chunks) { data.set(chunk, offset); offset += chunk.byteLength; }
        return {data, mime, url};
      }
      result = new Uint8Array(await result.arrayBuffer());
    } else if (result && 'data' in Object(result)) {
      mime = result.mime || ''; url = result.url || ''; result = result.data;
    }
    if (result instanceof Blob) {
      if (result.size > maxBytes) throw new Error('Resource exceeds size limit');
      mime ||= result.type; result = await result.arrayBuffer();
    }
    const data = result instanceof Uint8Array ? result
      : result instanceof ArrayBuffer ? new Uint8Array(result) : null;
    if (!data) throw new Error('Resource provider must return binary data');
    if (data.byteLength > maxBytes) throw new Error('Resource exceeds size limit');
    return {data, mime, url};
  }
  function load(request) {
    const id = ++sequence, controller = new AbortController();
    const started = performance.now();
    const record = {id, type:request.type || 'generic', source:request.url,
      state:'loading', started, bytes:0};
    active.set(id, record);
    const cancel = () => controller.abort();
    request.signal?.addEventListener('abort', cancel, {once:true});
    if (request.signal?.aborted) cancel();
    let timer, abortListener;
    const promise = (async () => {
      try {
        const url = new URL(request.url, request.baseURL || baseURL).href;
        record.url = url;
        const maxBytes = request.maxBytes ?? 32 * 1024 * 1024;
        const context = {id, url, source:request.url, type:record.type,
          signal:controller.signal, maxBytes};
        const aborted = new Promise((_, reject) => {
          abortListener = () => reject(abortError());
          controller.signal.addEventListener('abort', abortListener, {once:true});
          if (controller.signal.aborted) abortListener();
        });
        timer = setTimeout(() => {record.timedOut = true; cancel();}, timeout);
        const work = (async () => {
          if (controller.signal.aborted) throw abortError();
          let result;
          if (provider) result = await provider(context);
          else {
            if (!['http:', 'https:', 'data:', 'blob:'].includes(new URL(url).protocol))
              throw new Error('Unsupported resource URL protocol');
            result = await fetch(url, {signal:controller.signal});
          }
          const response = await bytes(result, maxBytes, controller.signal);
          if (controller.signal.aborted) throw abortError();
          return {...response, url:response.url || url};
        })();
        const response = await Promise.race([work, aborted]);
        record.bytes = response.data.byteLength;
        record.finalURL = response.url; record.state = 'loaded';
        return response;
      } catch (error) {
        record.state = record.timedOut ? 'error' : controller.signal.aborted ? 'cancelled' : 'error';
        record.error = record.timedOut ? 'Resource request timed out' : String(error.message || error);
        if (record.timedOut) throw new Error(record.error);
        throw error;
      } finally {
        clearTimeout(timer);
        controller.signal.removeEventListener('abort', abortListener);
        request.signal?.removeEventListener('abort', cancel);
        record.duration = performance.now() - started;
        active.delete(id); history.push(record);
        if (history.length > 128) history.shift();
      }
    })();
    return {id, promise, cancel};
  }
  return {load, inspect:() => [...history, ...active.values()].map(r => ({...r}))};
};

globalThis.createLynxBrowserFonts = function(module, resources, overrides = {}) {
  const files = ['Roboto-Regular.ttf', 'NotoSansSC.ttf', 'NotoSansArabic.ttf', 'NotoEmoji.ttf'];
  const records = files.map((file, slot) => ({slot, url:overrides[slot] || `fonts/${file}`, state:'idle'}));
  const scheduled = new Map();
  let epoch = 0;
  function load(slot) {
    const r = records[slot];
    if (!r) return Promise.reject(new Error('Unknown font slot'));
    if (r.state === 'loaded') return Promise.resolve();
    if (r.state === 'loading') return r.promise;
    if (r.state === 'error') return Promise.reject(new Error(r.error));
    const generation = epoch;
    r.state = 'loading'; r.error = '';
    const handle = resources.load({url:r.url, type:'font'});
    r.handle = handle;
    r.promise = (async () => {
      try {
        const response = await handle.promise;
        if (epoch !== generation || r.handle !== handle) return;
        let pointer = module._malloc(response.data.byteLength);
        if (!pointer) throw new Error('Could not allocate font bytes');
        try {
          module.HEAPU8.set(response.data, pointer);
          const code = module.ccall('register_browser_font', 'number',
            ['number', 'number', 'number'], [slot, pointer, response.data.byteLength]);
          if (code) throw new Error('Invalid or unsupported font data');
        } finally { module._free(pointer); }
        r.state = 'loaded'; r.bytes = response.data.byteLength;
      } catch (error) {
        if (epoch === generation && r.handle === handle) {
          r.state = 'error'; r.error = String(error.message || error);
        }
        throw error;
      } finally { if (r.handle === handle) r.handle = null; }
    })();
    return r.promise;
  }
  return {
    initialize:() => load(0),
    request(slot) {
      // Native measurement is synchronous. Start I/O after it returns; a
      // candidate that is discarded or replaces its page cannot retain work.
      const generation = epoch;
      if (records[slot]?.state !== 'idle' || scheduled.get(slot) === generation) return;
      scheduled.set(slot, generation);
      queueMicrotask(() => {
        if (scheduled.get(slot) === generation) scheduled.delete(slot);
        if (epoch !== generation || records[slot]?.state !== 'idle') return;
        void load(slot).catch(() => {});
      });
    },
    beginPage() {
      ++epoch;
      scheduled.clear();
      for (const r of records) if (r.state !== 'loaded') {
        r.handle?.cancel(); r.handle = null; r.promise = null;
        r.state = 'idle'; r.error = '';
      }
    },
    retry(slot) {
      const r = records[slot];
      if (r?.state === 'error') r.state = 'idle';
      return load(slot);
    },
    inspect:() => records.map(({slot, url, state, error, bytes}) => ({slot, url, state, error, bytes})),
  };
};
