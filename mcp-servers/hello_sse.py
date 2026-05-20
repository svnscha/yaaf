from __future__ import annotations

import argparse
import json
from http.server import ThreadingHTTPServer
from pathlib import Path

from hello_common import encode_response
from hello_http import HelloHttpHandler


class HelloSseHandler(HelloHttpHandler):
    response_content_type = "text/event-stream"

    def format_response(self, response: dict) -> bytes:
        return f"event: message\ndata: {encode_response(response)}\n\n".encode("utf-8")


def run() -> int:
    parser = argparse.ArgumentParser(description="Run the yaaf hello SSE MCP fixture server.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--ready-file")
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), HelloSseHandler)
    if args.ready_file:
        Path(args.ready_file).write_text(
            json.dumps({"host": args.host, "port": server.server_port}), encoding="utf-8"
        )
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
