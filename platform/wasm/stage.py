# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

"""Stage an atomic, self-contained HTML preview with no runtime asset requests."""
from pathlib import Path
import argparse
import json
import re
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--target', default='static_canvas', choices=['static_canvas', 'bundle_canvas'])
parser.add_argument('--site', default='out/Wasm/site')
parser.add_argument('--devtool', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
output = root / "out/Wasm"
template_file = 'bundle-index.html' if args.target == 'bundle_canvas' else 'index.html'
template = (root / 'platform/wasm' / template_file).read_text()
module = (output / (args.target + '.js')).read_text()
marker = "/* LYNX_WASM_MODULE */"
if template.count(marker) != 1:
    raise RuntimeError("The HTML template must contain exactly one module marker")
if "var createLynxCanvas =" not in module:
    raise RuntimeError("The generated JS does not export the expected factory")
if "return binaryDecode(" not in module:
    raise RuntimeError("Rebuild with SINGLE_FILE=1 before staging")
module = re.sub(r"</script", r"<\\/script", module, flags=re.IGNORECASE)
html = template.replace(marker, module + "\nglobalThis.createLynxCanvas = createLynxCanvas;")
if args.target == 'bundle_canvas':
    image_adapter = (root / 'platform/wasm/browser-bitmap-animation.js').read_text() + '\n' + (root / 'platform/wasm/browser-images.js').read_text()
    if html.count('/* LYNX_BROWSER_IMAGES */') != 1:
        raise RuntimeError('The bundle HTML must contain exactly one image adapter marker')
    image_adapter = re.sub(r'</script', r'<\\/script', image_adapter, flags=re.IGNORECASE)
    html = html.replace('/* LYNX_BROWSER_IMAGES */', image_adapter)
if args.target == 'bundle_canvas':
    engine = (root / 'platform/wasm/browser-engine.js').read_text() + '\n' + (root / 'platform/wasm/browser-client.js').read_text()
    if html.count('/* LYNX_BROWSER_ENGINE */') != 1:
        raise RuntimeError('Missing browser engine marker')
    html = html.replace('/* LYNX_BROWSER_ENGINE */', re.sub(r'</script', r'<\\/script', engine, flags=re.IGNORECASE))
site = root / args.site
site.mkdir(parents=True, exist_ok=True)
temporary = site / "index.html.new"
temporary.write_text(html)
temporary.replace(site / "index.html")
shutil.copy2(root / "out/Roboto-OFL.txt", site / "Roboto-OFL.txt")
if args.devtool:
    # The compiler places source maps inside debug-metadata.json. Serve its
    # original maps at their declared paths for the official Sources panel.
    metadata = root / 'platform/wasm/app/dist/.rspeedy/main/debug-metadata.json'
    if not metadata.is_file():
        raise RuntimeError('Build the DevTool fixture in development mode first')
    for artifact in json.loads(metadata.read_text()).get('artifacts', []):
        for source in artifact.get('debugSources', []):
            if source.get('kind') != 'source-map' or 'map' not in source:
                continue
            relative = Path(source['path'])
            if relative.is_absolute() or '..' in relative.parts:
                raise RuntimeError('Invalid compiler source-map path')
            destination = site / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(json.dumps(source['map']))
print(f"Staged self-contained HTML: {len(html.encode())} bytes")
