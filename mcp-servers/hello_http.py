from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from hello_common import encode_response, handle_request


class HelloHttpHandler(BaseHTTPRequestHandler):
    response_content_type = "application/json"

    def do_GET(self) -> None:
        if self.path == "/health":
            self.send_payload(200, b"ok", "text/plain")
            return
        self.send_payload(404, b"not found", "text/plain")

    def do_POST(self) -> None:
        if self.path != "/mcp":
            self.send_payload(404, b"not found", "text/plain")
            return

        content_length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(content_length).decode("utf-8")
        response = handle_request(json.loads(body))
        if response is None:
            self.send_response(202)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        payload = self.format_response(response)
        session_header = response.get("result", {}).get("serverInfo") is not None
        self.send_payload(200, payload, self.response_content_type, session_header=session_header)

    def log_message(self, format: str, *args: object) -> None:
        return

    def format_response(self, response: dict) -> bytes:
        return encode_response(response).encode("utf-8")

    def send_payload(self, status: int, payload: bytes, content_type: str, session_header: bool = False) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        if session_header:
            self.send_header("Mcp-Session-Id", "hello-session")
        self.end_headers()
        self.wfile.write(payload)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the yaaf hello HTTP MCP fixture server.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--ready-file")
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), HelloHttpHandler)
    if args.ready_file:
        Path(args.ready_file).write_text(
            json.dumps({"host": args.host, "port": server.server_port}), encoding="utf-8"
        )
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
