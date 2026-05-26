# Lua Process API Contracts & Shared Native Process Abstraction

## Research Complete: API Specifications for Phase 2 Implementation

This document captures the API contracts established during Phase 1 Discovery.

### Shared Native Process C++ Interface

```cpp
namespace yaaf::process {

struct ProcessOptions {
    std::string command;
    std::vector<std::string> args;
    std::filesystem::path working_directory;
    std::map<std::string, std::string> env_overrides;
    bool inherit_parent_env = true;
};

struct ReadResult {
    bool timed_out = false;
    bool process_exited = false;
    std::string data;
};

class PlatformProcess {
  public:
    virtual ~PlatformProcess() = default;
    virtual void write(std::string_view data) = 0;
    virtual ReadResult read_line(std::chrono::milliseconds timeout) = 0;
    virtual bool has_exited() const = 0;
    virtual void shutdown(std::chrono::milliseconds wait_timeout = std::chrono::seconds(1)) = 0;
};

std::unique_ptr<PlatformProcess> start_process(const ProcessOptions& options);

} // namespace yaaf::process
```

### Lua Process Module API

```lua
local process = require("process")

local handle = process.start({
    command = "/usr/bin/python3",
    args = {"./mcp_server.py"},
    cwd = "/srv/tools",
    env = { API_KEY = "secret" },
    inherit_env = true
})

handle:write('{"jsonrpc":"2.0",...}\n')
local line, err = handle:read(5000)  -- error: nil, "timeout", "exited", or message
if handle:is_alive() then ... end
handle:shutdown(1000)
handle:close()
```

### Platform Mapping

- **`yaaf.platform`**: One of `windows`, `linux`, `osx`
- Always set at startup, immutable during script execution

### Error Mapping

| Case | Behavior |
|------|----------|
| Spawn/Write/Init error | Lua error (throw) |
| Read timeout/exit | Return tuple: `(nil, "timeout")` or `(nil, "exited")` |
| Read I/O error | Return tuple: `(nil, "error message")` |

### Current MCP Stdio Findings

**Files involved:**
- `libyaaf/mcp/mcp_client_stdio.h` — Abstract base + factory
- `libyaaf/mcp/mcp_client_stdio.posix.cpp` — Uses `posix_spawnp`
- `libyaaf/mcp/mcp_client_stdio.win32.cpp` — Uses `CreateProcessA`

**Key parameters:**
- command, args (JSON array), env overrides, envFile
- stdin/stdout pipes for JSON-RPC messaging
- 1-second graceful wait before SIGTERM/TerminateProcess

**Refactor approach:**
- Extract spawn/pipe logic into shared `yaaf::process` layer
- Keep MCP stdio as thin JSON-RPC wrapper

### File Layout After Implementation

```
libyaaf/
├── process/
│   ├── process.h
│   ├── process_posix.cpp
│   ├── process_win32.cpp
│   └── CMakeLists.txt
├── platform/
│   ├── platform_name.h
│   └── platform_name.cpp
├── script/modules/
│   ├── script_process.h
│   ├── script_process.cpp
│   └── (register in lua_runtime.h)
└── mcp/
    ├── mcp_client_stdio.h (refactored)
    ├── mcp_client_stdio.posix.cpp (refactored)
    └── mcp_client_stdio.win32.cpp (refactored)
```

### Implementation Risks & Mitigations

1. **POSIX cwd not settable via `posix_spawnp`**: Use `fork()`+`chdir()`+`execvpe()` instead; test on Linux/macOS
2. **Windows quoting edge cases**: Migrate MCP's existing quote_command_part() and cover in unit tests
3. **Process handle leak**: Ensure Lua `__gc` metatable is set; test with `collectgarbage("collect")`
4. **Relative vs absolute cwd**: Document API expects absolute paths; caller resolves relatives
5. **Read partial line buffering**: Current design one-line-at-a-time (JSON-RPC scope); binary protocols need future extension
6. **Environment unbounded growth**: Build fresh env array vs inherit+override (status quo); add sanity checks

---

## Phase 2 Implementation Plan

Ready to proceed with:
1. Create shared process abstraction in `libyaaf/process/`
2. Add platform_name() in `libyaaf/platform/`
3. Refactor MCP stdio to use shared launcher
4. Create Lua process module with native bindings
5. Extend yaaf.platform field
6. Add focused tests
7. Update documentation
