# Lynx / QuickJS / WebGPU WASM canvas host

This experiment runs a compiled ReactLynx `.lynx.bundle` in WebAssembly and
paints the native Lynx element tree into a browser canvas with Skity WebGPU.
There is no Lynxtron, CEF, GTK, Node runtime or browser DOM layout. The page
renderer is Skity/WebGPU. Canvas2D is used for animation patch composition in
the compatibility decoder, and for pixel readback by verification scripts; it
does not lay out or render the Lynx page.

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

## Execution threads

MTS QJS, native elements, Starlight layout and Skity/WebGPU rendering execute on
the browser main thread. BTS QJS executes on one persistent Emscripten pthread,
backed by a real Web Worker. The worker's WASM instance shares linear memory
with the main instance; C++ messages retain the Lynx runtime protocol rather
than serializing values through JSON.

- BTS construction, JS execution, timers, animation callbacks and destruction
  run on its worker. The page's actor is recreated on bundle reload, while the
  worker itself is reused. The UI never joins the worker during reload.
- Browser FML loops record their owning pthread. Cross-thread wakeups proxy a
  timer to that owner; user closures execute after returning to its event loop,
  never inside the low-level system proxy queue.
- BTS-to-MTS calls post to the main runner. Values backed by a QJS context are
  converted to native Lepus values before crossing threads. Page lifetime
  tokens discard retired callbacks before dereferencing an old host.
- Browser rAF drives two separate frame clocks. MTS callbacks run on main;
  BTS callbacks run on the worker. Worker tick signals coalesce to the latest
  timestamp while BTS is busy, preventing a backlog of obsolete frames.
- Image decoding/presentation remains on main, so GIF and main-thread input
  can keep updating during a long BTS script. Other generic report/cache tasks
  still use their caller's browser loop; this change isolates BTS specifically.

The bundle host requires SharedArrayBuffer and cross-origin isolation:
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. `serve.py` sets these headers and
`Cross-Origin-Resource-Policy: same-origin`. The self-contained module supplies
a Blob URL for pthread startup; no public server or extra runtime files are
needed. Worker startup failure is reported; there is no silent single-thread
fallback. The separate eight-node static probe keeps its non-pthread build.

Thread acceptance (`verify-bts-worker.mjs`) checks distinct thread IDs, one
browser Worker and shared WASM memory; a real 1.2-second QuickJS busy script
must leave main-thread drawing and MTS clicks responsive. BTS events wait and
resume afterward. It also checks ten counter updates, worker rAF, stopping,
reload while busy, and five repeated reloads without spawning more workers.
The `test_bts_busy` export exists only as an experimental acceptance hook.

```bash
# build-images.sh also compiles the interaction fixture for this verifier.
python3 platform/wasm/serve.py --port 6120 --site out/Wasm/images-site
PROBE_URL=http://127.0.0.1:6120/ node platform/wasm/verify-bts-worker.mjs
PROBE_BROWSER=webkit PROBE_URL=http://127.0.0.1:6120/ PROBE_OUTPUT=out/Wasm/validation-bts-worker-webkit node platform/wasm/verify-bts-worker.mjs
```

GPU surfaces are retained until their submission completes. Frame IDs allow
releasing only the completed prefix, and the queue is capped at two frames.
At the pinned Skity revision its WebGPU surface consumes the supplied texture
reference; the host transfers ownership and never releases that texture twice.

The local file picker accepts `.lynx.bundle` files compiled by the LynxJS
toolchain, such as `@lynx-js/rspeedy`. JSX/TSX/JS source is rejected. Selected
bytes stay in the browser and enter WASM through its heap. Candidate pages are
validated before replacing the current page; rejected files preserve the old
interactive page. Reloading destroys its old runtimes and pending callbacks.

## Browser-decoded images

`<image>` loads static PNG/JPEG/WebP using browser `fetch` +
`createImageBitmap`. The decoded bitmap is copied directly to a WebGPU RGBA
texture, wrapped by Skity and drawn through `DrawImageRect`. No image codec
libraries or decoded RGBA buffer are added to WASM. This avoids a browser-to-WASM
pixel copy; it does not promise zero copies inside the browser.

- Explicit element width/height; `aspectFit`, `aspectFill`, default
  `scaleToFill` and `center`. Fit centers the entire image; fill crops the
  source rectangle centrally. Transparent pixels use premultiplied alpha.
- `bindload` fires once per request, with original `detail.width/height`.
  `binderror` fires once on HTTP/CORS/format/decode failures, with
  `detail.errMsg` and host `detail.error_code = -1`.
- GIF and animated WebP prefer browser `ImageDecoder`. When it is absent or
  lacks the format, a browser-bitmap compatibility decoder parses container
  frame metadata, packages compressed patches as standalone GIF/WebP blobs and
  asks `createImageBitmap` to decode them. Canvas2D handles partial rectangles,
  alpha blending and disposal/restore; a composed bitmap enters the same Skity
  WebGPU texture path. There is no JavaScript LZW/VP8 decoder or WASM codec.
  Only one frame is decoded at a time, with its duration driving uploads on
  the existing rAF clock.
  `autoplay=false` displays the first frame without starting playback;
  attribute changes pause/resume. `loop-count=0` means infinite; positive
  values stop on the last frame after the specified number of full loops.
  `bindstartplay`, `bindcurrentloopcomplete` and `bindfinalloopcomplete` use
  real Lynx custom events. Hidden pages pause the playback clock. Visible slow
  frames still advance images; long elapsed intervals are capped rather than
  discarded, so a 300 ms rAF interval cannot freeze playback.
- Removing a node, clearing/changing `src` or accepting a new bundle cancels
  its fetch and decoder. Monotonic request tokens reject obsolete callbacks.
  Submitted GPU frames retain their sampled images until completion.
- HTTP(S), data and blob sources work within browser access rules. Relative
  URLs resolve against the preview document, not the uploaded bundle's local
  file location. External servers must allow CORS. Image support does not
  turn the rest of the host into a general network resource loader.
- Encoded images are limited to 32 MiB and decoded images to 16 million pixels
  plus the device texture-dimension limit. Decoder/browser/GPU allocations are
  outside WASM and still consume memory. Per-node resources are retained while
  mounted; cross-node URL deduplication is not implemented.

Animated GIF/WebP playback works without `ImageDecoder` when the browser can
statically decode GIF/WebP and supports `createImageBitmap` + Canvas2D.
The fallback caps animations at 4096 frames and allocates one composition
canvas (plus a backup only for GIF restore-previous disposal). Decoded pixels
remain outside WASM; these browser allocations still consume memory.
APNG detection is present but is not part of the accepted format matrix;
APNG currently requires a supported `ImageDecoder`. GIF plain-text graphics
extensions are explicitly rejected by the compatibility decoder.
Auto-size, placeholder, nine-patch, tint/blur, rounded clipping and selector
animation methods are not implemented by this image adapter.

Build and verify the image sample separately from the interaction preview:

```bash
bash platform/wasm/build-images.sh
python3 platform/wasm/serve.py --port 6096 --site out/Wasm/images-site
# In a separate shell, with Playwright and Chrome available:
node platform/wasm/verify-images.mjs
PROBE_NO_IMAGE_DECODER=1 PROBE_OUTPUT=out/Wasm/validation-images-bitmap node platform/wasm/verify-images.mjs
node platform/wasm/verify-bitmap-animation.mjs
# With Playwright WebKit installed, run the same end-to-end suite:
PROBE_BROWSER=webkit PROBE_OUTPUT=out/Wasm/validation-images-webkit node platform/wasm/verify-images.mjs
# Compare WebKit composition against the saved Chrome frame oracle:
PROBE_BROWSER=webkit node platform/wasm/verify-bitmap-animation.mjs
# After acceptance, publish only the local image preview subdirectory:
python3 platform/wasm/stage.py --target bundle_canvas --site out/Wasm/site/images
```

Optional environment variables: `PLAYWRIGHT_PATH`, `CHROME_PATH`, `PROBE_URL`,
`PROBE_OUTPUT`, `PROBE_BROWSER=webkit` and `PROBE_NO_IMAGE_DECODER=1`. The verifier also binds a local CORS fixture server to
127.0.0.1:6196; it uses no public image services. The committed tiny data-URI
fixtures can be regenerated with `generate-image-fixtures.py` (Pillow required).

Acceptance writes `out/Wasm/validation-images/validation.json`, `canvas.png`,
`browser.png` and `console.json`. It checks actual GPU pixels for all three
static formats, landscape and portrait fit/fill, alpha blending, real React
load/error payloads (including a main-thread load handler) and event counts,
autonomous browser-composited GIF frame changes without forced draws or React
updates, a 300 ms visible rAF clock, two-loop animated
WebP, autoplay and hidden-page pause/resume, src replacement/removal/reload, HTTP 404,
corrupt PNG decoding, allowed/denied CORS, cancelled pending requests,
obsolete callbacks, idle rendering and absence of native/WebGPU errors.
A second browser page checks successful playback without ImageDecoder.
The complete image suite also runs with ImageDecoder forcibly absent.
`verify-bitmap-animation.mjs` compares every composed pixel, timing, repeated
frame and two loops against Chrome ImageDecoder for normal and partial/alpha
GIF/WebP fixtures, including GIF disposal 2/3 and local palettes. Its WebKit
mode compares the same frame signatures against the saved Chrome oracle.
The complete suite also passes on desktop WebKit 26.5 with ImageDecoder absent,
including real bundle execution, Skity/WebGPU pixels, autonomous canvas redraw,
Play/Pause and lifecycle checks. The three deliberately denied CORS fixture
requests produce expected access-control page diagnostics in WebKit; all other
page/runtime/WebGPU errors must remain absent. Physical iPhone acceptance
still needs device verification. The existing
interaction verifier was also run against this image-enabled host after loading
the original interaction bundle: click pixels, ten counter updates, five-second
rAF animation, reload and bounded GPU queue all passed. Acceptance uses
macOS ARM64, Chrome 152, WebKit 26.5 and Apple WebGPU/Metal. Physical iPhone
and other browser/device combinations remain to be validated.

## TTText paragraphs

The bundle host now measures and paints text with the pinned public TTText
engine (`dependencies/revision.py:TEXTRA`), its HarfBuzz shaper and substitute
bidi implementation. The browser adapter uses TTText's Skity canvas/typeface
interfaces. Browser DOM and Canvas2D do not lay out or paint this text.

- A page owns its paragraph/layout cache. The same final-width LayoutRegion
  supplies line positions and Skity glyph drawing. Destroying/reloading the
  page releases its paragraphs; font managers are shared on the main thread.
- UTF-8 content, Chinese/Latin wrapping, explicit newlines, automatic height,
  fixed line height, center/right alignment, long-word wrapping, max-line
  ellipsis and single-line clipping are supported. The external font set covers
  Latin, Simplified Chinese, Arabic and monochrome emoji. HarfBuzz performs
  glyph shaping; fonts fall back by character rather than replacing text with
  browser-generated bitmaps.
- Nested text runs preserve parent styles and apply their own CSS color,
  size, weight, italic and decoration. The host maps Lynx's unspecified
  spacing markers to TTText defaults instead of adding negative spacing.
- Text/width/style updates rebuild the affected paragraph and update sibling
  layout. `bindlayout` uses real Lynx events and UTF-16 line offsets, including
  supplementary characters. Painting does not generate new layout events.
- `scene_report` exposes element IDs, line geometry, run styles, submitted
  glyph counts, missing glyphs and contextual glyph substitutions for local
  acceptance/debugging. This is the experimental browser text adapter; Clay's
  component tree and platform TextLayoutAPI are not yet connected.

```bash
bash platform/wasm/build-text.sh
python3 platform/wasm/serve.py --port 6121 --site out/Wasm/text-site
PROBE_URL=http://127.0.0.1:6121/ node platform/wasm/verify-text.mjs
PROBE_BROWSER=webkit PROBE_OUTPUT=out/Wasm/validation-text-webkit node platform/wasm/verify-text.mjs
```

The verifier uses a real ReactLynx bundle and checks line measurements against
the native node layout, actual Chinese/rich-text WebGPU pixels, contextual
Arabic glyph substitutions, CJK/emoji font fallback, UTF-16 event offsets,
dynamic text/width/color updates, moving following siblings, idle events and
three reloads with a persistent BTS worker. Acceptance artifacts are written
under `out/Wasm/validation-text` and `out/Wasm/validation-text-webkit`.

All four test fonts are external `fonts/*.ttf` files, with OFL notices staged
beside the HTML. No font data is embedded in the bundle host WASM. Roboto loads
before native startup; Simplified Chinese, Arabic and Emoji load when the text
contains the corresponding Unicode ranges or explicitly requests their known
families. Optional fonts do not block first paint. Their arrival clears
paragraph, family-match and shape caches and requests native layout, including
following siblings and real layout events. Loaded faces remain shared across
page reloads. Failed optional fonts remain observable, with explicit retry.
Arbitrary font-family registration, inline images/views, inline truncation
widgets/events, text gradients/shadows, selection and editing are not
implemented. Font coverage is limited to the supplied assets; this does not
access the browser's installed system fonts.

## Web resource provider

Images and fonts use the same `module.lynxResources` asynchronous provider.
It resolves relative URLs against the preview document, supplies resource type,
request ID, AbortSignal and size limit, and records bounded diagnostics (last
128 completed requests plus current requests). The default implementation uses
browser fetch, including browser HTTP caching and CORS rules, with a 30-second
timeout and a 32 MiB encoded-resource limit, enforced while reading streams.
Image decode/upload stays in the browser; font bytes enter WASM and are parsed
by Skity/FreeType. Resource responses do not carry decoded pixels through WASM.
No additional application byte cache or cross-node request deduplication is
implemented. Bundle/lazy-bundle/external-JS loaders are not wired into this
provider yet; this is not a full LynxResourceLoader implementation.

A host may define these before creating the preview instance:

```js
// Optional: use the same request interface for file/DevTools/custom schemes.
window.lynxResourceProvider = async ({url, type, signal, maxBytes, id}) => {
  return fetch(url, {signal});
};
// Slots: 0 Latin, 1 Simplified Chinese, 2 Arabic, 3 Emoji.
window.lynxFontResources = {1: '/assets/NotoSansSC.ttf'};
```

Providers return a Response, Blob, ArrayBuffer, Uint8Array, or
`{data, mime, url}`. Browser CORS still applies to provider fetches. Custom
providers must respect cancellation to release their own resources; the bridge
rejects cancelled/timed-out requests and ignores late results even if they do
not. Font page generations and image tokens reject obsolete callbacks. An
accepted page replacement cancels pending optional font loads; the new page
can request them again. A rejected bundle leaves the current page intact.

Diagnostics and optional-font retry:

```js
lynxCanvas.lynxResources.inspect();
lynxCanvas.lynxFonts.inspect();
await lynxCanvas.lynxFonts.retry(1);
```

`verify-resource-provider.mjs` checks the provider contract, stream limits,
cancellation, late completion and timeout. `verify-web-resources.mjs` checks
real native font registration/reflow, ASCII-only demand, HTTP/corrupt-font
failure and retry, reload cancellation and a shared custom provider for image
and font consumers. Run the latter against a staged text site containing
`interactive/` and `images/` variants built with the same current host:

```bash
bash platform/wasm/build-resources.sh
python3 platform/wasm/serve.py --port 6121 --site out/Wasm/text-site
node platform/wasm/verify-resource-provider.mjs
node platform/wasm/verify-web-resources.mjs
PROBE_URL=http://127.0.0.1:6121/images/ \
  PROBE_IMAGE_BUNDLE=out/Wasm/images-resource.lynx.bundle node platform/wasm/verify-images.mjs
PROBE_URL=http://127.0.0.1:6121/interactive/ \
  PROBE_INITIAL_BUNDLE=out/Wasm/interactive-resource.lynx.bundle node platform/wasm/verify-interactive.mjs
node platform/wasm/measure-resource-sizes.mjs
```

Set `PROBE_BROWSER=webkit` and separate `PROBE_OUTPUT` directories to run
browser acceptance on WebKit as well. `measure-resource-sizes.mjs` validates
the extracted WASM, checks that none of the four font file headers are embedded,
and writes raw/gzip/Brotli sizes and hashes to `out/Wasm/resource-sizes.json`.

## Limits

This is an experimental canvas host, not complete Lynx platform support.
The renderer supports `view`/`text`/`image`, solid colors, fixed viewport layout,
TTText shaping/wrapping and nested styled text. Scrolling, general CSS
transform/opacity/clipping, native modules,
bundle/external-JS network loading, keyboard/IME and accessibility remain unsupported.
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
`out/Wasm/bundle-site`. The HTML embeds its loader, WASM, bundle and core JS;
font assets are staged separately. Font files are fetched separately through the provider.

```bash
python3 platform/wasm/serve.py --port 6093 --site out/Wasm/bundle-site
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
- No uncaught JS, native-runtime or WebGPU errors; one document request and
  the external fonts needed by the displayed page.

Verified locally with macOS ARM64, Chrome 152 and Apple WebGPU/Metal.
The five-second animation checkpoint delivered 300 native state updates and
300 new frames. This is a functional acceptance result, not a cross-device
performance benchmark.

## Revisions and build scope

- Lynx public base: `377c684b893d8c99033ad42d7e7198112592f389`.
- PrimJS: `f78a8bb4d727bc80d9a142df0de25fe72bb168c1`.
- Skity: `d68693d19457fed75ceb8584a66417361e9cc00c`.
- TTText/Textra: `14a7c148b59fc44505a9e871c6f90eeeed065054`. HarfBuzz uses
  the version/checksum pinned by Emscripten 4.0.20.
- Frontend: ReactLynx 0.123.1, Rspeedy 0.16.1, React plugin 0.18.1.
  Dependencies are pinned by `app/package-lock.json`.
- Font files are external to the bundle WASM. Current text WASM: 15.20 MB
  raw, 2.60 MB gzip, 2.16 MB Brotli. HTML: 16.73 MB raw, 2.82 MB gzip.
  The full Chinese font is a separate 17.77 MB file, fetched on demand.
  Compression sizes are measured; the LAN preview does not apply HTTP
  content encoding. See `out/Wasm/resource-sizes.json` for exact bytes and
  hashes. Debug names are retained and LTO is disabled;
  these are experiment sizes, not production-size measurements.
- Browser-only adaptations use the opt-in `enable_wasm_static_canvas` flag.
  Native platform builds keep their existing implementations.
- Generic report/cache tasks use their caller's browser event loop. BTS has its
  dedicated pthread/Worker. Bytecode caching
  lives inside the WASM virtual `/tmp`, with no host filesystem access.

The original eight-node component probe remains separately available:

```bash
bash platform/wasm/build.sh
node platform/wasm/verify.mjs
```

It executes hand-written `scene.js` in QJS and maps JSON to Starlight nodes;
it reports `fullLynxBundle: false`. Set `LYNX_CANVAS_INTERACTIVE=0` when building
the bundle host to use the earlier static ReactLynx sample instead.
