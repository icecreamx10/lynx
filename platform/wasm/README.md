# Lynx / QuickJS / WebGPU WASM canvas host

This experiment runs a compiled ReactLynx `.lynx.bundle` in WebAssembly and
paints the native Lynx element tree into a browser canvas with Skity WebGPU.
There is no Lynxtron, CEF, GTK, Node runtime, browser DOM layout or Canvas2D
production renderer. Canvas2D is used only by the verification scripts to
read back test pixels.

## Current capabilities

The default sample provides three independently verified paths:

1. `main-thread:bindtap` changes a card's color inside the main-thread QJS
   runtime. Browser pointer events are mapped into canvas coordinates, hit
   tested against the native render tree and dispatched through Lynx's
   `TemplateAssembler::SendTouchEvent` pipeline.
2. Ordinary `bindtap` executes in a second QJS context, with the real Lynx
   `BTSRuntime`, public-source `lynx_core.js` and ReactLynx background bundle.
   `useState` updates return through Lynx messages and `CallLepusMethod` to
   the original native element tree. Context-owned values are converted to
   native Lepus values before crossing runtime boundaries.
3. Browser rAF drives Lynx animation-frame callbacks. The sample uses React
   state to move a native rectangle and update a text node continuously.
   Only dirty pages submit GPU draws. Stopping animation or leaving the page
   unchanged stops drawing; the scheduler still observes browser frame ticks.

Both QJS contexts currently execute asynchronously on **one browser thread**.
This preserves separate runtime contexts and the framework message protocol,
without requiring pthreads, shared WASM memory, or cross-origin isolation.
Moving background execution to a Worker is future work. Heavy background JS
can still block the browser in this version.

GPU surfaces are retained until their submission completes. Frame IDs allow
releasing only the completed prefix, and the queue is capped at two frames.
At the pinned Skity revision its WebGPU surface consumes the supplied texture
reference; the host transfers ownership and never releases that texture twice.

The local file picker accepts `.lynx.bundle` files compiled by the LynxJS
toolchain, such as `@lynx-js/rspeedy`. JSX/TSX/JS source is rejected. Selected
bytes stay in the browser and enter WASM through its heap. Candidate pages are
validated before replacing the current page; rejected files preserve the old
interactive page. Reloading destroys its old runtimes and pending callbacks.

## Limits

This is an experimental canvas host, not complete Lynx platform support.
The renderer supports `view`/`text`, solid colors, fixed viewport layout and
single-line ASCII text with Skity font metrics. Full text shaping, wrapping,
images, rich text, scrolling, CSS transform/opacity/clipping, native modules,
network resources, keyboard/IME and accessibility remain unsupported.
The demonstrated animation moves nodes through layout updates, rather than
claiming a complete native CSS animation backend.

The viewport is 800 × 500. Pointer coordinates account for CSS canvas scaling;
drags exceeding the tap threshold and canceled pointers do not generate taps.
The paint and hit-test adapters currently use the render tree's order and
untransformed rectangles. More complete clipping, transforms and stacking
support must evolve together.

## Build and preview

Requirements: Python 3.11+, Node 20.11+, npm, Git, CMake, Ninja and activated
Emscripten **4.0.20**. Public dependency revisions are checked before applying
their declared patches. Existing checkouts are never reset by preparation.

```bash
export LYNX_WASM_EMSDK=/absolute/path/to/emsdk
bash platform/wasm/build-bundle.sh
```

This compiles the JSX/CSS sample, builds Lynx Core JS from this checkout,
builds the native WASM host and stages the candidate separately under
`out/Wasm/bundle-site`. The HTML embeds its loader, WASM, bundle, core JS and
font, so it makes no separate runtime asset requests.

```bash
python3 -m http.server 6093 --bind 127.0.0.1 --directory out/Wasm/bundle-site
PLAYWRIGHT_PATH=/path/to/playwright CHROME_PATH=/path/to/chrome \
  node platform/wasm/verify-interactive.mjs

# Promote only after validation:
python3 platform/wasm/stage.py --target bundle_canvas
python3 platform/wasm/serve.py
```

For LAN HTTPS, supply a local certificate and its private key:

```bash
python3 platform/wasm/serve.py --bind YOUR_LAN_IP --port 6092 \
  --cert /path/to/preview.crt --key /path/to/preview.key
```

The server serves only `out/Wasm/site` and sends `Cache-Control: no-store`.
It does not expose the source checkout. WebGPU requires a secure context;
localhost HTTP works for development, while LAN access should use HTTPS.
There is no public deployment or tunneling.

## Acceptance

`verify-interactive.mjs` verifies a real compiled bundle and writes
`out/Wasm/validation-interactive/validation.json`, `browser.png` and
`animation.png`. The gates are:

- A native main-thread click changes actual GPU pixels, then toggles back.
- Ordinary `bindtap` + `useState` reaches ten successive increments.
- Continuous animation changes native text and rectangle layout, with at
  least 60 state updates and rendered frames over five seconds.
- Stopping the animation stops frame submission; idle pages do not redraw.
- Native GPU queue holds at most two frames; a third submission is rejected.
  Completed-prefix release and repeated release leave the remaining lease intact.
- CSS-scaled pointer coordinates work, and dragging does not trigger tap.
- Reload during animation cancels the old page and resets the new counter.
- Invalid bundle input preserves a page whose counter still works afterward.
- No uncaught JS, native-runtime or WebGPU errors; only one document request.

Verified locally with macOS ARM64, Chrome 152 and Apple WebGPU/Metal.
The five-second animation checkpoint delivered 300 native state updates and
300 new frames. This is a functional acceptance result, not a cross-device
performance benchmark.

## Revisions and build scope

- Lynx public base: `377c684b893d8c99033ad42d7e7198112592f389`.
- PrimJS: `f78a8bb4d727bc80d9a142df0de25fe72bb168c1`.
- Skity: `d68693d19457fed75ceb8584a66417361e9cc00c`.
- Frontend: ReactLynx 0.123.1, Rspeedy 0.16.1, React plugin 0.18.1.
  Dependencies are pinned by `app/package-lock.json`.
- Current diagnostic WASM: 14.34 MB, gzip 2.53 MB. Self-contained HTML:
  15.83 MB, gzip 2.75 MB. Debug names are retained and LTO is disabled;
  these are experiment sizes, not production-size measurements.
- Browser-only adaptations use the opt-in `enable_wasm_static_canvas` flag.
  Native platform builds keep their existing implementations.
- Report tasks and worker tasks use the browser event loop. Bytecode caching
  lives inside the WASM virtual `/tmp`, with no host filesystem access.

The original eight-node component probe remains separately available:

```bash
bash platform/wasm/build.sh
node platform/wasm/verify.mjs
```

It executes hand-written `scene.js` in QJS and maps JSON to Starlight nodes;
it reports `fullLynxBundle: false`. Set `LYNX_CANVAS_INTERACTIVE=0` when building
the bundle host to use the earlier static ReactLynx sample instead.
