"""Stands in for GET /api/v1/emulators/ps5sx2/game-index (server/app/routes/emulators.py).

    python game_index_fixture.py <port file> <body file>

Writes the port it listens on to <port file>, serves <body file> with the
route's headers, and a wrong X-File-Hash to a request whose API key is "bad".
"""

import hashlib
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/api/v1/emulators/ps5sx2/game-index":
            self.send_error(404)
            return
        body = open(sys.argv[2], "rb").read()
        digest = hashlib.sha256(body).hexdigest()
        if self.headers.get("X-API-Key") == "bad":
            digest = "0" * 64
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-File-Hash", digest)
        self.send_header("X-Source-Tag", "vk-test")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


server = HTTPServer(("127.0.0.1", 0), Handler)
with open(sys.argv[1], "w") as f:
    f.write(str(server.server_address[1]))
server.serve_forever()
