# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

"""Stage an atomic, self-contained HTML preview with no runtime asset requests."""
from pathlib import Path
import re
import shutil

root = Path(__file__).resolve().parents[2]
output = root / "out/Wasm"
template = (root / "platform/wasm/index.html").read_text()
module = (output / "static_canvas.js").read_text()
marker = "/* LYNX_WASM_MODULE */"
if template.count(marker) != 1:
    raise RuntimeError("The HTML template must contain exactly one module marker")
if "var createLynxCanvas =" not in module:
    raise RuntimeError("The generated JS does not export the expected factory")
if "return binaryDecode(" not in module:
    raise RuntimeError("Rebuild with SINGLE_FILE=1 before staging")
module = re.sub(r"</script", r"<\\/script", module, flags=re.IGNORECASE)
html = template.replace(marker, module + "\nglobalThis.createLynxCanvas = createLynxCanvas;")
site = output / "site"
site.mkdir(parents=True, exist_ok=True)
temporary = site / "index.html.new"
temporary.write_text(html)
temporary.replace(site / "index.html")
shutil.copy2(root / "out/Roboto-OFL.txt", site / "Roboto-OFL.txt")
print(f"Staged self-contained HTML: {len(html.encode())} bytes")
