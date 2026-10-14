# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0.
"""Package only staged public demo assets for a static HTTPS host."""
import argparse
import json
from pathlib import Path
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--site', default='out/public-site/dist')
parser.add_argument('--devtool-site', default='out/Wasm/devtool-site')
parser.add_argument('--fixtures-site', default='out/Wasm/text-site')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
site, devtool, fixtures = (root / value for value in
                         [args.site, args.devtool_site, args.fixtures_site])
site.mkdir(parents=True, exist_ok=True)
for name in ['fonts', 'devtools', '.rspeedy']:
    source = devtool / name
    if source.is_dir():
        shutil.copytree(source, site / name, dirs_exist_ok=True)
# Some static hosts exclude dot directories from their asset manifest.
if (devtool / '.rspeedy').is_dir():
    shutil.copytree(devtool / '.rspeedy', site / 'source-maps', dirs_exist_ok=True)
host = site / 'devtools/host.js'
transport = host.read_text()
old = 'message.params.sourceMapURL, location.origin'
new = "message.params.sourceMapURL.replace(/^\\/\\.rspeedy\\//, '/source-maps/'), location.origin"
if transport.count(old) != 1:
    raise RuntimeError('Unexpected DevTool source-map URL bridge')
host.write_text(transport.replace(old, new))
for source in devtool.glob('*OFL*'):
    if source.is_file():
        shutil.copy2(source, site / source.name)
nav = ('<nav style="display:flex;gap:16px;flex-wrap:wrap;margin-bottom:20px">'
       + ''.join(f'<a style="color:#7eb9ff" href="{url}">{label}</a>' for url, label in [
           ('/', 'DevTool'), ('/text/', 'Text / 中文'), ('/images/', 'Images / GIF'),
           ('/interactive/', 'Counter / Animation'),
           ('https://github.com/icecreamx10/lynx/issues/1', 'Build guide / GitHub')])
       + '</nav>')
fonts = '<script>window.lynxFontResources=' + json.dumps({
    i: '/fonts/' + name for i, name in enumerate([
        'Roboto-Regular.ttf', 'NotoSansSC.ttf', 'NotoSansArabic.ttf', 'NotoEmoji.ttf'])
}) + ';</script>'
for source, destination in [
        (devtool / 'index.html', site / 'index.html'),
        (fixtures / 'index.html', site / 'text/index.html'),
        (fixtures / 'images/index.html', site / 'images/index.html'),
        (fixtures / 'interactive/index.html', site / 'interactive/index.html')]:
    html = source.read_text()
    # Wait inside the host error boundary before it checks isolation or creates WASM.
    marker = "    if(!navigator.gpu)" if destination == site / 'index.html' else "    if (typeof globalThis.createLynxCanvas"
    if marker not in html: raise RuntimeError('Missing engine startup boundary')
    html = html.replace(marker, '    await window.lynxIsolationReady;\n' + marker, 1)
    html = html.replace('<script>Error.stackTraceLimit', '<script src="/isolation-bootstrap.js"></script>\n<script>Error.stackTraceLimit', 1)
    if '/isolation-bootstrap.js' not in html:
        html = html.replace('<script id="lynx-wasm-module">', '<script src="/isolation-bootstrap.js"></script>\n<script id="lynx-wasm-module">', 1)
    html = html.replace('<h1>Lynx Bundle · WASM Canvas</h1>',
                        '<h1>Lynx Bundle · WASM Canvas</h1>' + nav + fonts)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(html)
for name in ['isolation-worker.js', 'isolation-bootstrap.js']:
    shutil.copy2(root / 'platform/wasm' / name, site / name)
(site / '_headers').write_text('/*\n  Cross-Origin-Opener-Policy: same-origin\n'
                              '  Cross-Origin-Embedder-Policy: require-corp\n'
                              '  Cross-Origin-Resource-Policy: same-origin\n')
assets = [p for p in site.rglob('*') if p.is_file()]
for asset in assets:
    if asset.stat().st_size > 25 * 1024 * 1024:
        raise RuntimeError(f'Asset exceeds 25 MiB: {asset.relative_to(site)}')
print(f'Staged {len(assets)} static assets; fonts shared across demos')
