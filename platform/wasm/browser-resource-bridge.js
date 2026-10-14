// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
// Requests run through the Window provider; native registration and decoders
// consume owned responses on the render thread, including after an MTS pause.
function createLynxWorkerResourceProvider(module) {
  const pending = new Map();
  let next = 1;
  function load(request) {
    const id = next++;
    let rejectRequest;
    const cancel = () => {
      const entry = pending.get(id);
      if (!entry) return;
      pending.delete(id);
      request.signal?.removeEventListener('abort', cancel);
      module.ccall('host_resource_cancel', null, ['number'], [id]);
      rejectRequest(new DOMException('Resource request cancelled', 'AbortError'));
    };
    const promise = new Promise((resolve, reject) => {
      rejectRequest = reject;
      if (pending.size >= 128) { reject(Error('Resource request queue is full')); return; }
      pending.set(id, {resolve, reject, cleanup: () => request.signal?.removeEventListener('abort', cancel)});
      request.signal?.addEventListener('abort', cancel, {once:true});
      if (request.signal?.aborted) { cancel(); return; }
      module.ccall('host_resource_request', null, ['number', 'string'], [id,
        JSON.stringify({url:request.url, type:request.type,
          baseURL:request.baseURL, maxBytes:Math.min(request.maxBytes ?? 32*1024*1024, 32*1024*1024)})]);
    });
    return {id, promise, cancel};
  }
  return {load, complete(id, data, mime, url, error) {
    const entry = pending.get(id);
    if (!entry) return;
    pending.delete(id); entry.cleanup();
    if (error) entry.reject(Error(error));
    else entry.resolve({data, mime, url});
  }};
}
