// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
export class LynxDevToolHost {
  constructor({container, dispatch, onDetach = () => {}}) {
    Object.assign(this,{container,dispatch,onDetach,generation:0,closed:true});
  }
  open(generation) {
    this.close();
    this.closed = false;
    this.generation = generation;
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
          this.dispatch(data.payload, this.generation);
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
    if (!this.closed && generation===this.generation && this.port)
      this.port.postMessage({kind:'cdp',generation,payload});
  }
  close() {
    if (this.closed) return;
    this.closed = true; this.attached = false;
    removeEventListener('message',this.listener);
    this.port?.postMessage({kind:'detached',generation:this.generation});
    this.port?.close(); this.port = null;
    this.iframe?.remove(); this.iframe = null;
    this.onDetach();
  }
}
