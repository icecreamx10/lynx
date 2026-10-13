# QuickJS / Starlight / WebGPU canvas probe

This experiment compiles Lynx's pinned PrimJS/QuickJS engine and Starlight
layout engine to WebAssembly, then paints their static scene with Skity's
WebGPU backend into a browser canvas. It does not depend on Lynxtron, Node at
runtime, CEF, GTK, browser DOM layout, or Canvas2D rendering.

## Scope

The scene script runs inside QuickJS in WASM. A small C++ scene adapter maps its
result to real Starlight layout nodes, and Skity draws rounded rectangles and
font glyphs directly to the WebGPU canvas texture.

**This is a component integration proof, not a complete Lynx browser host.**
It does not load `.lynx.bundle`, run the Lynx TemplateAssembler/shell, or include
Clay's full UI, text shaping, events, IME, resource services or accessibility.
The browser report explicitly sets `fullLynxBundle: false`. The sample text uses
Skity's simple text API with explicit node sizes, rather than Clay text layout.

The separate full `core:lynx_native` compilation probe has not passed. Its most
recent stop was an existing NAPI enum constexpr conversion rejected by the
new Emscripten compiler; this component target avoids that dependency. Passing
this canvas probe must not be interpreted as full Lynx runtime support.

## Reproduce

Requirements: Python 3.11+, Git, npm, CMake, Ninja, and activated Emscripten
**4.0.20**. The dependency preparation script fetches public repositories at
the revisions declared by Lynx and Skity, and applies their declared patches.
It verifies existing revisions and does not reset existing checkouts.

```bash
export LYNX_WASM_EMSDK=/absolute/path/to/emsdk
bash platform/wasm/build.sh
python3 platform/wasm/serve.py
```

Open <http://127.0.0.1:6091/>. The server exposes only `out/Wasm/site`. The HTML
embeds the JS factory, WASM, scene script and font data; rendering does not
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
errors and writes `validation.json`, `canvas.png` and `browser.png`.

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
- Scene: 800 × 500, 8 Starlight nodes, 13 frames including cleanup and redraw.
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
