# http

`http` is a built-in module that exposes the native low-level HTTP bridge for Lua scripts and provider implementations.

```lua
local http = require("http")
```

## Functions

- `http.get(request)`: perform a GET request.
- `http.post(request)`: perform a POST request.

## Request Shape

Both functions accept a table with these fields:

- `url`: required request URL.
- `headers`: optional table of request header name/value pairs.

`http.post(request)` also accepts:

- `body`: optional request body string. Defaults to an empty string.
- `content_type`: optional content type string. Defaults to `application/json`.
- `on_response_chunk`: optional callback that receives streamed response chunks.

## Response Shape

Both functions return a table with:

- `status_code`: numeric HTTP status.
- `content_type`: response content type if present.
- `body`: full response body.
- `headers`: table of response header name/value pairs.
