# process

`process` is a built-in module that provides process spawning and inter-process communication capabilities for Lua scripts.

Load it with:

```lua
local process = require("process")
```

## API Reference

### process.start(config) → handle

Spawns a new child process and returns a handle for interaction.

**Parameters:**

- `config` (table, required): Configuration table with the following fields:
  - `command` (string, required): The executable to run (e.g., `"echo"`, `"/usr/bin/python3"`).
  - `args` (array of strings, optional): Command-line arguments to pass to the executable.
  - `cwd` (string, optional): Working directory for the child process. If not specified, inherits from parent.
  - `env` (table of key-value pairs, optional): Environment variables to set. If `inherit_env` is `true`, these override parent environment.
  - `inherit_env` (boolean, optional, default: `true`): If `true`, inherit parent environment variables. If `false`, only use `env` variables.

**Returns:** A process handle object for reading, writing, and managing the child process.

**Errors:** Throws an error if:
- `command` is missing or not a string
- `command` is an empty string
- The process fails to start (e.g., executable not found, permission denied)

### handle:write(data)

Writes data to the child process's stdin.

**Parameters:**

- `data` (string, required): Data to write to stdin.

**Errors:** Throws an error on I/O failure.

### handle:read(timeout_ms) → (line, error_string)

Reads one line from the child process's stdout.

**Parameters:**

- `timeout_ms` (integer, optional, default: `5000`): Timeout in milliseconds. Use `0` for non-blocking read or `-1` for indefinite wait.

**Returns:** A pair of values:
- On success: `(line, nil)` where `line` is a string.
- On timeout: `(nil, "timeout")` if no data arrives within the timeout period.
- On process exit: `(nil, "exited")` if the process has exited and no more data is available.
- On I/O error: `(nil, error_message)` where `error_message` describes the error.

### handle:is_alive() → bool

Checks whether the child process is still running.

**Returns:** `true` if the process is alive, `false` if it has exited.

### handle:shutdown(timeout_ms)

Gracefully shuts down the child process.

**Parameters:**

- `timeout_ms` (integer, optional, default: `1000`): Timeout in milliseconds for graceful shutdown. After this period, forceful termination may occur.

**Errors:** Throws an error on failure (e.g., if the process handle is invalid).

### handle:close()

Explicitly closes and cleans up the process handle. This is called automatically by the garbage collector if not invoked manually.

**Errors:** Throws an error on failure (e.g., if the process handle is invalid).

## Errors

Common error messages and conditions:

- `"process handle is nil or invalid"`: The handle object is corrupted or has been already closed.
- `"process.start() requires a table argument"`: The argument to `process.start()` was not a table.
- `"process.start() requires 'command' field (string)"`: The `command` field is missing or not a string.
- `"process.start() 'command' cannot be empty"`: The `command` field is an empty string.
- `"timeout"`: The `read()` call timed out waiting for data.
- `"exited"`: The process has exited and no more data is available.
- Platform-specific I/O errors: May occur during `write()`, `read()`, or `shutdown()`.

## Example

The following example runs the `echo` command with arguments, captures its output, and performs cleanup:

```lua
local process = require("process")

-- Start echo with two arguments
local handle = process.start({
    command = "echo",
    args = { "Hello", "World" },
})

-- Read output with 2-second timeout
local line, err = handle:read(2000)

if err then
    print("Error: " .. err)
else
    print("Output: " .. line)
end

-- Check if process is still alive
if handle:is_alive() then
    print("Process still running")
else
    print("Process has exited")
end

-- Graceful shutdown (if still running)
if handle:is_alive() then
    handle:shutdown(1000)
end

-- Cleanup
handle:close()
```

For platform-specific process spawning, combine with [yaaf.platform](yaaf.md#fields) to adjust commands:

```lua
local process = require("process")
local yaaf = require("yaaf")

-- Use different commands based on platform
local cmd
if yaaf.platform == "windows" then
    cmd = "cmd"
else
    cmd = "/bin/sh"
end

local handle = process.start({
    command = cmd,
    args = { "-c", "echo Hello" },
})

local line, err = handle:read(2000)
handle:close()
```
