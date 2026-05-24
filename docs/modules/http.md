# http

`http` is a built-in module that exposes the native low-level, request-based HTTP bridge for Lua scripts and provider implementations.

```lua
local http = require("http")
```

## Functions

- `http.request(request)`: perform a request using the request table's `method`.
- `http.get(request)`: perform a GET request.
- `http.post(request)`: perform a POST request.
- `http.put(request)`: perform a PUT request.
- `http.patch(request)`: perform a PATCH request.
- `http.delete(request)`: perform a DELETE request.
- `http.head(request)`: perform a HEAD request.

All helpers return the same response shape as `http.request(request)`. The verb-specific helpers are thin adapters over the same native request execution path.

## Request Shape

Requests use a table with these fields:

- `method`: optional HTTP method string. Defaults to `"GET"` for `http.request(request)`.
- `url`: required request URL.
- `headers`: optional table of request header name/value pairs.
- `body`: optional request body string.
- `content_type`: optional content type string.
- `timeout`: optional total request timeout in milliseconds.
- `on_response_chunk`: optional callback that receives streamed response chunks.

Compatibility notes:

- `http.post(request)` keeps the previous defaults of an empty request body and `application/json` content type when those fields are omitted.
- `http.head(request)` returns the response headers and status code, and typically an empty body.
- `timeout` is a total request timeout for the whole request, not a separate connect timeout.

## Response Shape

All functions return a table with:

- `status_code`: numeric HTTP status.
- `content_type`: response content type if present.
- `body`: full response body.
- `headers`: table of response header name/value pairs.

## Non-Goals

The module is still a low-level transport bridge. It does not add retries, cookie jars, auth helpers, multipart uploads, WebSocket support, or a higher-level REST client abstraction.
