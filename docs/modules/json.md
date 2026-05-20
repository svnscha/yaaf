# json

`json` is a built-in module that exposes JSON encode and decode helpers.

Load it with:

```lua
local json = require("json")
```

## Functions

- `json.encode(value, pretty)`: encodes a Lua value as JSON. Set `pretty = true` for indented output.
- `json.decode(text)`: decodes JSON into Lua tables and scalar values.

Lua tables are encoded as arrays when they use contiguous integer keys starting at `1`; otherwise string and numeric keys become JSON object keys. `nil`, booleans, numbers, strings, arrays, and object-like tables are supported.

```lua
local payload = json.decode('{"answer":"ok"}')
print(json.encode({ answer = payload.answer }))
```
