# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0.
"""Stage a pinned official frontend with an explicit MessagePort connection."""
from pathlib import Path
import argparse, hashlib, re, shutil, tarfile, urllib.request
ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser()
p.add_argument('--site', default='out/Wasm/devtool-site')
a = p.parse_args()
cache = ROOT / 'out/devtool'
cache.mkdir(parents=True, exist_ok=True)
archive = cache / 'frontend.tar.gz'
url = 'https://github.com/lynx-family/lynx-devtool/releases/download/devtools-frontend-lynx-11/devtool.frontend.lynx_1.0.1789464070.tar.gz'
if not archive.exists(): urllib.request.urlretrieve(url, archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest() == '04729c7aca990653eb96b6d68281733eb2aa1d8bf387f792d904a8320a287d7c'
site = ROOT / a.site / 'devtools'
site.mkdir(parents=True, exist_ok=True)
with tarfile.open(archive) as tar: tar.extractall(site, filter='data')
frontend = site / 'front_end_1789464070'
# Pinned Rollup output: replace the factory, leaving its legacy path intact.
sdk = frontend / 'core/sdk/sdk.js'
s = sdk.read_text()
old = 'function lr(e){return new ar(e)}'
assert s.count(old) == 1, 'Pinned frontend factory changed'
sdk.write_text(s.replace(old, 'function lr(e){return globalThis.LynxMessagePortConnection?new globalThis.LynxMessagePortConnection(e):new ar(e)}'))
# The release applies showPanels to every TabbedPane, including source-file
# tabs and Styles/Computed. In explicit port mode filter only the main panels.
legacy = frontend / 'ui/legacy/legacy.js'
s = legacy.read_text()
old = 'if(r&&"undefined"!==r&&!r.split(",")?.includes(i))return;'
new = ('if(r&&"undefined"!==r&&'
       '(new URLSearchParams(location.search).get("transport")!=="messageport"||'
       'this.element.classList.contains("main-tabbed-pane"))&&'
       '!r.split(",")?.includes(i))return;')
assert s.count(old) == 1, 'Pinned frontend panel filter changed'
legacy.write_text(s.replace(old, new))
html = (site / 'inspector.html').read_text()
# Keep the preview self-hosted, including optional React plugin dependencies.
for name,url in {
 'acorn-walk.js':'https://cdn.jsdelivr.net/npm/acorn-walk@6.0.1/dist/walk.js',
 'react.js':'https://cdn.jsdelivr.net/npm/react@17.0.2/umd/react.production.min.js',
 'react-dom.js':'https://cdn.jsdelivr.net/npm/react-dom@17.0.2/umd/react-dom.production.min.js',
}.items():
 target = cache / name
 if not target.exists(): urllib.request.urlretrieve(url, target)
 shutil.copy2(target, site/name)
 html = html.replace(url, './'+name)
html = html.replace('<meta charset="utf-8" />','<meta charset="utf-8" /><script src="./connection.js"></script>')
(site/'inspector.html').write_text(html)
for name in ['connection.js','host.js']: shutil.copy2(Path(__file__).parent/name,site/name)
print(site)
