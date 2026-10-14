// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
// Runs unchanged on the selected render thread. All native tree, GPU objects,
// frame leases and image decoders stay on that owner for the instance lifetime.
async function startLynxBrowserEngine(module, baseURL, worker) {
  const call = (name, result = 'number', types = [], args = []) =>
      module.ccall(name, result, types, args);
  const notify = (name, value, id) => id === undefined ?
      call(name, null, ['string'], [JSON.stringify(value)]) :
      call(name, null, ['number', 'string'], [id, JSON.stringify(value)]);
  let pumping = false, suspended = false, snapshotting = false, loading = false,
      inFlight = 0, lastReport = 0;
  let device, adapter;
  const report = () => JSON.parse(call('scene_report', 'string'));
  const state = () => ({
    report: report(),
    images: module.lynxImages.inspect(),
    imageStats: {...module.lynxImages.stats},
    fonts: module.lynxFonts.inspect(),
    worker
  });
  function fail(error) {
    pumping = false;
    notify('host_publish', {error: String(error.stack || error)});
  }
  const signatures = {
    scene_report: ['string', []],
    host_error_count: ['number', []],
    dispatch_pointer:
        ['number', ['number', 'number', 'number', 'number', 'number']],
    test_bts_busy: [null, ['number']],
    verify_corrupt_bundle: ['number', []],
  };
  let requests = Promise.resolve();
  module.lynxEngine = {
    dispatch(id, method, args) {
      requests = requests.then(() => dispatch(id, method, args));
    }
  };
  async function dispatch(id, method, args) {
    try {
      let result;
      if (method === 'load_bundle') {
        loading = true;
        try {
          await device.queue.onSubmittedWorkDone();
          call('release_completed_frames', null);
          result = call('load_bundle', 'number', ['number', 'number'], args);
          if (result) throw Error(call('bundle_load_error', 'string'));
          const code = call('render_scene');
          if (code) throw Error('Frame submission failed: ' + code);
          await device.queue.onSubmittedWorkDone();
          call('release_completed_frames', null);
          result = state();
        } finally {
          loading = false;
        }
      } else if (method === 'suspend') {
        suspended = !!args[0];
        if (suspended) module.lynxImages.suspend();
      } else if (method === 'snapshot') {
        snapshotting = true;
        try {
          await device.queue.onSubmittedWorkDone();
          call('release_completed_frames', null);
          const frame = call('render_frame');
          if (frame < 0) throw Error('Snapshot submission failed');
          const copy = typeof document === 'undefined' ?
              new OffscreenCanvas(800, 500) :
              document.createElement('canvas');
          copy.width = 800;
          copy.height = 500;
          const ctx = copy.getContext('2d');
          ctx.drawImage(module.canvas, 0, 0);
          const pixels =
              (args[0] ||
               []).map(([x, y]) => [...ctx.getImageData(x, y, 1, 1).data]);
          await device.queue.onSubmittedWorkDone();
          call('release_frames', null, ['number'], [frame]);
          result = {pixels, ...state()};
        } finally {
          snapshotting = false;
        }
      } else if (method === 'font_retry') {
        await module.lynxFonts.retry(args[0]);
        result = state();
      } else if (method === 'state')
        result = state();
      else {
        const signature = signatures[method];
        if (!signature || args.length !== signature[1].length)
          throw Error('Unsupported host request: ' + method);
        result = call(method, signature[0], signature[1], args);
      }
      notify('host_reply', {ok: true, result: result ?? null}, id);
    } catch (error) {
      notify(
          'host_reply', {ok: false, error: String(error.stack || error)}, id);
    }
  }
  function pump(time) {
    if (!pumping) return;
    if (!suspended && !snapshotting && !loading) {
      call('tick', null, ['number'], [time]);
      if (call('host_error_count')) {
        fail(Error(report().errors.join('; ')));
        return;
      }
      if (inFlight < 2 && call('needs_frame')) {
        const frame = call('render_frame');
        if (frame < 0) {
          fail(Error('Frame submission failed'));
          return;
        }
        ++inFlight;
        device.queue.onSubmittedWorkDone()
            .then(() => {
              call('release_frames', null, ['number'], [frame]);
              --inFlight;
            })
            .catch(fail);
      }
      if (time - lastReport > 250) {
        notify('host_publish', state());
        lastReport = time;
      }
    }
    requestAnimationFrame(pump);
  }
  try {
    adapter = await navigator.gpu.requestAdapter();
    if (!adapter)
      throw Error('No WebGPU adapter on the selected render thread');
    device = await adapter.requestDevice();
    module.preinitializedWebGPUDevice = device;
    module.lynxBaseURL = baseURL;
    if (worker) {
      module.lynxResourceBridge = createLynxWorkerResourceProvider(module);
      module.lynxResources = module.lynxResourceBridge;
    }
    module.lynxFonts = createLynxBrowserFonts(
        module, module.lynxResources, module.lynxFontResources || {});
    module.lynxImages = createLynxBrowserImages(module, device);
    await module.lynxFonts.initialize();
    device.addEventListener('uncapturederror', event => fail(event.error));
    device.lost.then(
        info => fail(Error('WebGPU device lost: ' + info.message)));
    const code = call('main');
    if (code)
      throw Error(
          'WASM initialization failed: ' + code + ': ' +
          call('bundle_load_error', 'string'));
    await device.queue.onSubmittedWorkDone();
    call('release_completed_frames', null);
    pumping = true;
    requestAnimationFrame(pump);
    notify('host_ready', {
      ok: true,
      ...state(),
      adapter: {
        vendor: adapter.info.vendor,
        architecture: adapter.info.architecture,
        device: adapter.info.device
      }
    });
  } catch (error) {
    notify('host_ready', {ok: false, error: String(error.stack || error)});
  }
}
