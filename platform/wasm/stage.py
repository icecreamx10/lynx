# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

"""Stage an atomic HTML preview with external font resources."""
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
if args.target == 'bundle_canvas':
    image_adapter = (root / 'platform/wasm/browser-resources.js').read_text() + '\n' + (root / 'platform/wasm/browser-bitmap-animation.js').read_text() + '\n' + (root / 'platform/wasm/browser-images.js').read_text()
    if html.count('/* LYNX_BROWSER_IMAGES */') != 1:
        raise RuntimeError('The bundle HTML must contain exactly one image adapter marker')
    image_adapter = re.sub(r'</script', r'<\\/script', image_adapter, flags=re.IGNORECASE)
    html = html.replace('/* LYNX_BROWSER_IMAGES */', image_adapter)
site = root / args.site
site.mkdir(parents=True, exist_ok=True)
temporary = site / "index.html.new"


def stage_asset(source, destination):
    asset_temporary = destination.with_name(destination.name + '.new')
    shutil.copy2(source, asset_temporary)
    asset_temporary.replace(destination)


stage_asset(root / "out/Roboto-OFL.txt", site / "Roboto-OFL.txt")
if args.target == 'bundle_canvas':
    (site / 'fonts').mkdir(exist_ok=True)
    for font in ['Roboto-Regular.ttf', 'NotoSansSC.ttf', 'NotoSansArabic.ttf', 'NotoEmoji.ttf']:
        stage_asset(root / 'out' / font, site / 'fonts' / font)
    for font in ['NotoSansSC.ttf', 'NotoSansArabic.ttf', 'NotoEmoji.ttf']:
        stage_asset(root / 'out' / (font + '.OFL.txt'), site / (font + '.OFL.txt'))
temporary.write_text(html)
temporary.replace(site / "index.html")
print(f"Staged HTML: {len(html.encode())} bytes")
