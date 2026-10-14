// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
// Wait before instantiating WASM. A static host without COOP/COEP needs one
// controlled navigation; a host supplying the headers needs no registration.
window.lynxIsolationReady = (async () => {
  const key = 'lynx-isolation-reload';
  if (crossOriginIsolated) { sessionStorage.removeItem(key); return; }
  if (!isSecureContext || !navigator.serviceWorker)
    throw Error('WASM workers require HTTPS and cross-origin isolation');
  if (sessionStorage.getItem(key)) {
    sessionStorage.removeItem(key);
    throw Error('The browser could not enable cross-origin isolation');
  }
  await navigator.serviceWorker.register('/isolation-worker.js', {scope:'/'});
  await navigator.serviceWorker.ready;
  if (!navigator.serviceWorker.controller) await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { cleanup(); reject(Error('Isolation worker activation timed out')); }, 10000);
    function cleanup() { clearTimeout(timer); navigator.serviceWorker.removeEventListener('controllerchange', ready); }
    function ready() { if (navigator.serviceWorker.controller) { cleanup(); resolve(); } }
    navigator.serviceWorker.addEventListener('controllerchange', ready);
    ready();
  });
  sessionStorage.setItem(key, '1');
  location.reload();
  await new Promise(() => {});
})();
// The engine consumes this rejection and shows it in its existing status UI.
window.lynxIsolationReady.catch(() => {});
