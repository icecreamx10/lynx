// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
// Loaded before the official frontend. Only the explicit port mode uses this.
(() => {
  const params = new URLSearchParams(location.search);
  if (params.get('transport') !== 'messageport') return;
  const token = params.get('token');
  let current;
  globalThis.LynxMessagePortConnection = class {
    constructor() {
      if (current) throw new Error('One DevTool connection per iframe');
      current = this;
      this.queue = [];
      this.pending = [];
      this.closed = false;
      this.listener = event => {
        if (event.origin !== location.origin || event.source !== parent ||
            event.data?.kind !== 'lynx-devtool-connect' || event.data.token !== token ||
            event.data.version !== 1 || event.ports.length !== 1 || this.port) return;
        this.generation = event.data.generation;
        this.port = event.ports[0];
        this.port.onmessage = ({data}) => {
          if (data?.generation !== this.generation) return;
          if (data.kind === 'detached') { this.disconnect(); return; }
          if (data.kind !== 'cdp' || typeof data.payload !== 'string') return;
          if (this.onMessage) this.onMessage(data.payload);
          else if (this.pending.length < 256) this.pending.push(data.payload);
          else this.disconnect();
        };
        this.port.onmessageerror = () => this.disconnect();
        this.port.start();
        this.port.postMessage({kind:'attached', generation:this.generation, version:1});
        for (const message of this.queue) this.sendRawMessage(message);
        this.queue.length = 0;
        removeEventListener('message', this.listener);
      };
      addEventListener('message', this.listener);
      parent.postMessage({kind:'lynx-devtool-ready',version:1,token},location.origin);
    }
    setOnMessage(callback) {
      this.onMessage = callback;
      for (const message of this.pending.splice(0)) callback(message);
    }
    setOnDisconnect(callback) { this.onDisconnect = callback; }
    sendRawMessage(payload) {
      if (this.closed) return;
      if (this.port) this.port.postMessage({kind:'cdp',generation:this.generation,payload});
      else if (this.queue.length < 256) this.queue.push(payload);
      else this.disconnect();
    }
    async disconnect() {
      if (this.closed) return;
      this.closed = true;
      removeEventListener('message', this.listener);
      this.port?.postMessage({kind:'detached',generation:this.generation});
      this.port?.close();
      this.queue.length = this.pending.length = 0;
      this.onMessage = null;
      this.onDisconnect?.('MessagePort disconnected');
      this.onDisconnect = null;
    }
  };
})();
