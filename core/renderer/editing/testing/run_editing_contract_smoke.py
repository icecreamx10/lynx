#!/usr/bin/env python3
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main() -> None:
    repo = Path(__file__).resolve().parents[4]
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    if not compiler:
        raise SystemExit("No C++ compiler found; set CXX to run the smoke test")

    sources = [
        "core/renderer/editing/edit_context_model.cc",
        "core/renderer/editing/editing_platform_contract.cc",
        "core/renderer/editing/editing_projection.cc",
        "core/renderer/editing/editing_host_controller.cc",
        "core/renderer/editing/editing_host_registry.cc",
        "core/renderer/editing/testing/editing_contract_smoke.cc",
    ]
    with tempfile.TemporaryDirectory(prefix="lynx_editing_smoke_") as output:
        executable = Path(output) / "editing_contract_smoke"
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{repo}",
            *(str(repo / source) for source in sources),
            "-o",
            str(executable),
        ]
        subprocess.run(command, cwd=repo, check=True)
        subprocess.run([str(executable)], cwd=repo, check=True)


if __name__ == "__main__":
    main()
