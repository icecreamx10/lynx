# QuickJS / Starlight / WebGPU canvas probe

This experiment compiles Lynx's pinned PrimJS/QuickJS engine and Starlight
layout engine to WebAssembly, then paints their static scene with Skity's
WebGPU backend into a browser canvas. It does not depend on Lynxtron, Node at
runtime, CEF, GTK, browser DOM layout, or Canvas2D rendering.

## Scope

There are two independent targets:

- `static_canvas`: the original component probe. A hand-written script in QJS
  returns JSON, which a C++ adapter maps to Starlight nodes. Its report sets
  `fullLynxBundle: false`.
- `bundle_canvas`: a real ReactLynx JSX/CSS app, compiled by Rspeedy into
  `main.lynx.bundle`. The WASM host calls `TemplateAssembler::LoadTemplate`,
  uses the real LepusNG/QJS main-thread runtime, and lets ElementManager resolve
  CSS and perform Starlight layout in `LAYOUT_IN_ELEMENT` mode. Skity paints the
  resulting native element tree directly into WebGPU. The report checks the
  runtime type and exposes the actual decoded text and layout tree.

The browser UI includes a local `.lynx.bundle` file picker. Files must be
compiled with the LynxJS toolchain (for example `@lynx-js/rspeedy`); JSX/TSX/JS
source files are not loadable. The bytes stay in the browser and are passed to
WASM through its heap. A candidate page is decoded and validated before it
replaces the current page, so a rejected bundle preserves the existing display.
The UI limits input to 64 MiB and reports loader/capability errors explicitly.

**The bundle target supports static first paint, not a complete browser host.**
Its compiled bundle includes background-thread JS, but that runtime is not
executed (`backgroundRuntime: false`). Interactions, React background lifecycle,
state updates, native modules, resources, IME and accessibility are not connected.
The canvas adapter currently paints solid backgrounds and single-line ASCII text
using Skity font metrics; it does not implement full text shaping, rich text,
wrapping, clipping, images or Clay UI. The sample deliberately uses fixed sizes.

Full `core:lynx_native` WASM compilation and linking now pass. The opt-in host
uses browser timers for FML and a single-thread task queue; it does not silently
start unsupported POSIX report or worker threads. Browser VSync is backed by
requestAnimationFrame with a nominal frame budget. Platform configuration is
`Web`; remote trail configuration and outbound telemetry are disabled by the
existing default platform implementations.

## Reproduce

Requirements: Python 3.11+, Git, npm, CMake, Ninja, and activated Emscripten
**4.0.20**. The dependency preparation script fetches public repositories at
the revisions declared by Lynx and Skity, and applies their declared patches.
It verifies existing revisions and does not reset existing checkouts.

```bash
export LYNX_WASM_EMSDK=/absolute/path/to/emsdk
bash platform/wasm/build-bundle.sh
# After validating the candidate, promote it to the live preview:
python3 platform/wasm/stage.py --target bundle_canvas
python3 platform/wasm/serve.py
```

The bundle script stages the candidate under `out/Wasm/bundle-site` first.
Serve it locally for validation:

```bash
python3 -m http.server 6093 --bind 127.0.0.1 --directory out/Wasm/bundle-site
PROBE_BUNDLE=1 PROBE_DOCUMENT_ONLY=1 PROBE_URL=http://127.0.0.1:6093/ \
  node platform/wasm/verify.mjs
```

The original baseline is still reproducible with `bash platform/wasm/build.sh`.

Open <http://127.0.0.1:6091/>. The server exposes only `out/Wasm/site`. The HTML
embeds the JS factory, WASM, bundle (or scene script) and font data; rendering does not
require any separate runtime asset requests. The font license is also staged.
The server sends `Cache-Control: no-store` to avoid mixing old HTML and new
runtime artifacts during this experiment.

For LAN HTTP access:

```bash
python3 platform/wasm/serve.py --bind YOUR_LAN_IP --port 6091
```

WebGPU normally requires a secure context. For this local HTTP experiment,
each visiting Chrome browser must explicitly allow the exact origin:

1. Open `chrome://flags/#unsafely-treat-insecure-origin-as-secure`.
2. Add `http://YOUR_LAN_IP:6091`, enable the option, and relaunch Chrome.
3. Open the HTTP preview again.

Without that exception the page shows a clear explanation and does not fake
the GPU result. HTTPS can alternatively be enabled with `--cert` and `--key`.
No external hosting, tunneling or public deployment is involved.

## Validate

Install Playwright in your tooling environment. `CHROME_PATH` optionally selects
a locally installed Chrome; `PLAYWRIGHT_PATH` can point to an existing Playwright
package. The test checks actual pixel colors, visible text, 12 rendered frames,
frame resource cleanup, and a subsequent 13th frame. It rejects WebGPU and WASM
errors and writes `validation.json`, `canvas.png` and `browser.png`. With
`PROBE_BUNDLE=1`, it also checks all six decoded text strings, CSS positions,
LepusNG runtime type, bundle loading success, and rejection of an invalid bundle
in an independent instance. A 14th frame proves that rejection leaves the
original displayed page intact. The UI check additionally uploads a valid
bundle, rejects damaged bytes and source files, then loads successfully again.
It verifies that file loading makes no additional network requests.

```bash
node platform/wasm/verify.mjs

# Regression: block every request except the HTML document itself.
PROBE_DOCUMENT_ONLY=1 node platform/wasm/verify.mjs

# For LAN HTTP only, explicitly grant the origin to this test browser:
PROBE_URL=http://YOUR_LAN_IP:6091/ \
PROBE_ALLOW_HTTP_ORIGIN=http://YOUR_LAN_IP:6091 \
node platform/wasm/verify.mjs
```

Canvas2D is used only by the test to sample the already rendered GPU canvas.
The production draw path does not read pixels back to the CPU.

## Verified checkpoint

- Public Lynx base: `377c684b893d8c99033ad42d7e7198112592f389`.
- PrimJS: `f78a8bb4d727bc80d9a142df0de25fe72bb168c1`.
- Skity: `d68693d19457fed75ceb8584a66417361e9cc00c`.
- Emscripten: 4.0.20, `emdawnwebgpu` port.
- macOS ARM64, Chrome 152.0.7977.83, Apple WebGPU / Metal 3 adapter.
- Baseline: 800 × 500, 8 Starlight nodes, 13 frames including cleanup and redraw.
- Bundle app: 86,416-byte ReactLynx bundle, 13 native elements, six text nodes.
  Browser validation passed with document-only loading, five pixel checks,
  six text/layout checks, resource cleanup, invalid-bundle rejection and
  redraw after rejection (14 frames total).
  Frontend versions: ReactLynx 0.123.1, Rspeedy 0.16.1, React plugin 0.18.1;
  transitive dependencies are pinned in `app/package-lock.json`.
- Measured diagnostic build (decimal MB): WASM 12.76 MB / gzip 2.30 MB;
  self-contained HTML 14.11 MB / gzip 2.50 MB. WASM includes the embedded font
  and app bundle. The preview server currently serves uncompressed HTML.
  Debug names are retained and LTO is disabled; these are experiment sizes.
- Five background/card pixel checks and visible text passed; no GPU/WASM errors.
- The build script and repeated dependency preparation passed locally.

The native buildroot patch removes an ELF-only linker flag for WASM and uses
the C++ linker. The probe enables the virtual filesystem for fonts and scene
data and excludes QuickJS's signal-based inspector/profiler. Native builds keep
their existing behavior because the experiment flag defaults to false.

At this Skity revision the WebGPU surface takes ownership of the supplied
texture without adding a reference. The host therefore transfers that reference
and releases each surface once after GPU completion. Releasing the same texture
again caused allocator corruption in the initial prototype; the cleanup/redraw
test covers the corrected ownership.
