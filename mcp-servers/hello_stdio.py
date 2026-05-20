from __future__ import annotations

import json
import sys

from hello_common import encode_response, handle_request


def main() -> int:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            response = handle_request(json.loads(line))
        except json.JSONDecodeError as exc:
            print(encode_response({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": str(exc)}}), flush=True)
            continue
        if response is not None:
            print(encode_response(response), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

