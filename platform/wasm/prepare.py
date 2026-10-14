# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

"""Fetch the small set of public dependencies needed by the canvas probe.

Existing dependency checkouts must match their declared revisions. This never
resets an existing checkout or overwrites an existing dependency directory.
"""

import glob
import hashlib
import json
import os
from pathlib import Path
import platform
import runpy
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))


def run(*args, cwd=ROOT, check=True):
    return subprocess.run(args, cwd=cwd, check=check)


def git_dependency(path, url, revision):
    if path.exists():
        actual = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=path, text=True).strip()
        if actual != revision:
            raise RuntimeError(f"{path}: expected {revision}, found {actual}")
        return
    path.mkdir(parents=True)
    run("git", "init", str(path))
    run("git", "remote", "add", "origin", url, cwd=path)
    run("git", "fetch", "--depth=1", "origin", revision, cwd=path)
    run("git", "checkout", "--detach", "FETCH_HEAD", cwd=path)


def apply_patch(path, patch):
    metadata = Path(subprocess.check_output(
        ["git", "rev-parse", "--absolute-git-dir"], cwd=path, text=True).strip())
    stamp = metadata / "lynx-canvas-applied-patches.json"
    applied = json.loads(stamp.read_text()) if stamp.exists() else []
    digest = hashlib.sha256(patch.read_bytes()).hexdigest()
    if digest in applied:
        return
    if subprocess.run(["git", "apply", "--reverse", "--check", str(patch)],
                      cwd=path, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
        pass
    else:
        run("git", "apply", "--check", str(patch), cwd=path)
        run("git", "apply", str(patch), cwd=path)
    stamp.write_text(json.dumps(applied + [digest]))


def declared_dependency(owner, name, spec):
    path = owner / name
    git_dependency(path, spec["url"], spec["commit"])
    patches = spec.get("patches", [])
    if isinstance(patches, str):
        patches = [patches]
    for pattern in patches:
        for patch in sorted(glob.glob(pattern)):
            apply_patch(path, Path(patch).resolve())


def main():
    deps = runpy.run_path(str(ROOT / "dependencies/DEPS"),
                         init_globals={"root_dir": str(ROOT)})["deps"]
    for name in ["build", "third_party/quickjs/src", "third_party/modp_b64"]:
        declared_dependency(ROOT, name, deps[name])
    if os.environ.get("PROBE_INSPECTOR") == "1":
        declared_dependency(ROOT, "third_party/zlib", deps["third_party/zlib"])
    apply_patch(ROOT / "build", ROOT / "platform/wasm/patches/buildroot-wasm.patch")
    apply_patch(ROOT / "build", ROOT / "platform/wasm/patches/buildroot-wasm-threads.patch")

    apply_patch(ROOT / "third_party/quickjs/src",
                ROOT / "platform/wasm/patches/primjs-wasm-profiler.patch")

    shared = runpy.run_path(str(ROOT / "dependencies/DEPS.tools_shared"),
                           init_globals={"root_dir": str(ROOT)})["deps"]
    declared_dependency(ROOT, "buildtools/tools_shared", shared["buildtools/tools_shared"])
    gn = ROOT / "buildtools/gn/gn"
    if not gn.exists():
        system = platform.system().lower()
        machine = platform.machine().lower()
        machine = "x86_64" if machine == "amd64" else machine
        url = f"https://github.com/lynx-family/buildtools/releases/download/gn-cc28efe6/buildtools-gn-{system}-{machine}.tar.gz"
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            archive = directory / "gn.tar.gz"
            urllib.request.urlretrieve(url, archive)
            with tarfile.open(archive) as tar:
                tar.extractall(directory / "unpacked", filter="data")
            binary = next(p for p in (directory / "unpacked").rglob("gn") if p.is_file())
            gn.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(binary, gn)
            gn.chmod(0o755)
    if not shutil.which("ninja") and not (ROOT / "buildtools/ninja/ninja").exists():
        raise RuntimeError("Install ninja or sync buildtools/ninja before building")

    # GN currently executes this setup even with page-facing NAPI disabled.
    package = ROOT / "third_party/weak-node-api"
    if not (package / "node_modules/@lynx-js/weak-node-api/package.json").exists():
        run("npm", "install", "--ignore-scripts", "--no-audit", "--no-fund", cwd=package)

    revision = runpy.run_path(str(ROOT / "dependencies/revision.py"))["SKITY"]
    skity = ROOT / "out/skity-source"
    git_dependency(skity, "https://github.com/lynx-family/skity.git", revision)
    graphics = runpy.run_path(str(skity / "hab/DEPS"),
                             init_globals={"root_dir": str(skity)})["deps"]
    for name in ["glm", "pugixml", "jsoncpp", "Vulkan-Headers", "freetype2", "nanopng"]:
        key = f"third_party/{name}"
        declared_dependency(skity, key, graphics[key])

    textra_revision = runpy.run_path(str(ROOT / "dependencies/revision.py"))["TEXTRA"]
    git_dependency(ROOT / "out/textra-source",
                   "https://github.com/lynx-family/lynx-textra.git", textra_revision)

    font = ROOT / "out/Roboto-Regular.ttf"
    if not font.exists():
        urllib.request.urlretrieve(
            "https://raw.githubusercontent.com/google/fonts/main/ofl/roboto/Roboto%5Bwdth,wght%5D.ttf", font)
    license_path = ROOT / "out/Roboto-OFL.txt"
    if not license_path.exists():
        urllib.request.urlretrieve(
            "https://raw.githubusercontent.com/google/fonts/main/ofl/roboto/OFL.txt", license_path)
    for family, filename, output, checksum in [
        ("notosanssc", "NotoSansSC%5Bwght%5D.ttf", "NotoSansSC.ttf",
         "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"),
        ("notoemoji", "NotoEmoji%5Bwght%5D.ttf", "NotoEmoji.ttf",
         "de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551"),
        ("notosansarabic", "NotoSansArabic%5Bwdth,wght%5D.ttf", "NotoSansArabic.ttf",
         "63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e"),
    ]:
        font_path = ROOT / "out" / output
        if not font_path.exists():
            urllib.request.urlretrieve(
                f"https://raw.githubusercontent.com/google/fonts/main/ofl/{family}/{filename}", font_path)
        if hashlib.sha256(font_path.read_bytes()).hexdigest() != checksum:
            raise RuntimeError(f"Unexpected font checksum: {font_path}")
        license_path = ROOT / "out" / (output + ".OFL.txt")
        if not license_path.exists():
            urllib.request.urlretrieve(
                f"https://raw.githubusercontent.com/google/fonts/main/ofl/{family}/OFL.txt", license_path)
    print("Canvas probe dependencies ready.")


if __name__ == "__main__":
    main()
