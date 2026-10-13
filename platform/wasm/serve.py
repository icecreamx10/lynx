# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

"""Serve only the staged canvas demo; WebGPU on LAN addresses needs HTTPS."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
from pathlib import Path
import ssl

parser = argparse.ArgumentParser()
parser.add_argument("--bind", default="127.0.0.1")
parser.add_argument("--port", type=int, default=6091)
parser.add_argument("--cert")
parser.add_argument("--key")
parser.add_argument("--site", default="out/Wasm/site")
args = parser.parse_args()
if not ipaddress.ip_address(args.bind).is_loopback and not args.cert:
    print("LAN HTTP: visiting browsers must explicitly allow this insecure origin for WebGPU.", flush=True)
directory = Path(__file__).resolve().parents[2] / args.site
if not directory.is_dir():
    parser.error("Build and stage the demo first")
class PreviewHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()


server = ThreadingHTTPServer((args.bind, args.port), partial(PreviewHandler, directory=str(directory)))
if args.cert:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.cert, args.key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
print(f"Serving {'https' if args.cert else 'http'}://{args.bind}:{args.port}/", flush=True)
server.serve_forever()
