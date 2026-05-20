from __future__ import annotations

import json
from typing import Any

LATEST_PROTOCOL_VERSION = "2025-11-25"
SUPPORTED_PROTOCOL_VERSIONS = {
    "2024-11-05",
    "2025-03-26",
    "2025-06-18",
    "2025-11-25",
}

TOOLS: list[dict[str, Any]] = [
    {
        "name": "hello",
        "title": "Hello",
        "description": "Return a friendly hello-world greeting.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name to greet."},
            },
        },
    },
    {
        "name": "repeat",
        "title": "Repeat",
        "description": "Repeat text a small number of times.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "text": {"type": "string", "description": "Text to repeat."},
                "count": {
                    "type": "integer",
                    "minimum": 1,
                    "maximum": 5,
                    "description": "Number of repetitions.",
                },
            },
            "required": ["text"],
        },
    },
]


def handle_request(message: dict[str, Any]) -> dict[str, Any] | None:
    method = message.get("method")
    request_id = message.get("id")

    if request_id is None:
        return None

    try:
        if method == "initialize":
            requested = message.get("params", {}).get("protocolVersion", LATEST_PROTOCOL_VERSION)
            protocol_version = requested if requested in SUPPORTED_PROTOCOL_VERSIONS else LATEST_PROTOCOL_VERSION
            return result(
                request_id,
                {
                    "protocolVersion": protocol_version,
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "yaaf-hello", "version": "0.1.0"},
                },
            )

        if method == "tools/list":
            return result(request_id, {"tools": TOOLS})

        if method == "tools/call":
            params = message.get("params", {})
            return result(request_id, call_tool(params.get("name", ""), params.get("arguments", {})))

        return error(request_id, -32601, f"unknown method: {method}")
    except Exception as exc:  # pragma: no cover - exercised through protocol errors in clients
        return error(request_id, -32603, str(exc))


def encode_response(response: dict[str, Any]) -> str:
    return json.dumps(response, separators=(",", ":"))


def result(request_id: Any, payload: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "result": payload}


def error(request_id: Any, code: int, message: str) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def call_tool(name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    if name == "hello":
        target = str(arguments.get("name") or "world")
        return text_result(f"Hello, {target}!")

    if name == "repeat":
        text = str(arguments.get("text") or "")
        count = int(arguments.get("count") or 2)
        count = max(1, min(count, 5))
        return text_result(" ".join([text] * count))

    return {
        "content": [{"type": "text", "text": f"unknown hello tool: {name}"}],
        "isError": True,
    }


def text_result(text: str) -> dict[str, Any]:
    return {"content": [{"type": "text", "text": text}], "isError": False}

