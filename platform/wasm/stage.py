# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

"""Stage an atomic, self-contained HTML preview with no runtime asset requests."""
from pathlib import Path
import argparse
import re
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--target', default='static_canvas', choices=['static_canvas', 'bundle_canvas'])
parser.add_argument('--site', default='out/Wasm/site')
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
site = root / args.site
site.mkdir(parents=True, exist_ok=True)
temporary = site / "index.html.new"
temporary.write_text(html)
temporary.replace(site / "index.html")
shutil.copy2(root / "out/Roboto-OFL.txt", site / "Roboto-OFL.txt")
print(f"Staged self-contained HTML: {len(html.encode())} bytes")
