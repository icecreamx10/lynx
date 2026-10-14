// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
// Wire ids remain unique across iframe reconnects. A late native response may
// otherwise collide with a new frontend's request id (which starts at 1).
let nextWireId = 1;
export class LynxDevToolHost {
  constructor({container, dispatch, onDetach = () => {}}) {
    Object.assign(this,{container,dispatch,onDetach,generation:0,closed:true});
  }
  open(generation) {
    this.close();
    this.closed = false;
    this.generation = generation;
    this.pending = new Map();
    this.token = crypto.randomUUID();
    this.iframe = document.createElement('iframe');
    this.iframe.title = 'Lynx DevTool';
    this.iframe.style.cssText = 'width:100%;height:100%;border:0;background:white';
    this.listener = event => {
      if (event.origin !== location.origin || event.source !== this.iframe.contentWindow ||
          event.data?.kind !== 'lynx-devtool-ready' || event.data.version !== 1 ||
          event.data.token !== this.token || this.port) return;
      const channel = new MessageChannel();
      this.port = channel.port1;
      this.port.onmessage = ({data}) => {
        if (this.closed || data?.generation !== this.generation) return;
        if (data.kind === 'attached') { this.attached = true; return; }
        if (data.kind === 'detached') { this.close(); return; }
        if (data.kind !== 'cdp' || typeof data.payload !== 'string' || data.payload.length > 4*1024*1024) return;
        try {
          const request = JSON.parse(data.payload);
          if (!request || typeof request.method !== 'string' || !Number.isSafeInteger(request.id)) return;
          if (this.pending.size >= 256 || !Number.isSafeInteger(nextWireId)) { this.close(); return; }
          const wireId = nextWireId++;
          this.pending.set(wireId, request.id);
          try {
            this.dispatch(JSON.stringify({...request,id:wireId}), this.generation);
          } catch(error) {
            this.pending.delete(wireId);
            throw error;
          }
        } catch(error) { console.error('DevTool dispatch failed', error); }
      };
      this.port.onmessageerror = () => this.close();
      this.port.start();
      this.iframe.contentWindow.postMessage({kind:'lynx-devtool-connect',token:this.token,
        version:1,generation:this.generation},location.origin,[channel.port2]);
    };
    addEventListener('message',this.listener);
    this.iframe.src = '/devtools/inspector.html?transport=messageport&token='+encodeURIComponent(this.token)
      +'&showPanels=Elements,Console,Sources&hl=en-US&panel=elements';
    this.container.replaceChildren(this.iframe);
  }
  send(payload,generation=this.generation) {
    if (this.closed || generation!==this.generation || !this.port) return;
    const message = JSON.parse(payload);
    if (Object.hasOwn(message,'id')) {
      if (!this.pending.has(message.id)) return;
      const id = this.pending.get(message.id);
      this.pending.delete(message.id);
      message.id = id;
    }
    // Native script URLs are virtual file:// URLs. Resolve the browser host's
    // root-relative source-map paths against the serving origin instead.
    if (message.method === 'Debugger.scriptParsed' &&
        message.params?.sourceMapURL?.startsWith('/') &&
        !message.params.sourceMapURL.startsWith('//')) {
      message.params.sourceMapURL = new URL(message.params.sourceMapURL, location.origin).href;
    }
    payload = JSON.stringify(message);
    this.port.postMessage({kind:'cdp',generation,payload});
  }
  close() {
    if (this.closed) return;
    this.closed = true; this.attached = false;
    this.pending?.clear();
    removeEventListener('message',this.listener);
    this.port?.postMessage({kind:'detached',generation:this.generation});
    this.port?.close(); this.port = null;
    this.iframe?.remove(); this.iframe = null;
    this.onDetach();
  }
}
