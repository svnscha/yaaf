# YAAF MCP Codebase Exploration – Comprehensive Analysis

**Date:** May 26, 2026  
**Purpose:** Detailed exploration of yaaf's current MCP client implementation, Lua runtime design, tool registry, and testing patterns to inform the MCP stdio host bridge feature.

---

## 1. Current MCP Client Structure (`libyaaf/mcp/`)

### File Organization
- `mcp_client.h` / `mcp_client.cpp` – Public client API and implementation
- `mcp_client_stdio.h` / `mcp_client_stdio.posix.cpp` / `mcp_client_stdio.win32.cpp` – Stdio transport
- `mcp_schema.h` – Schema registry interfaces
- `mcp_schema_generated.h` / `mcp_schema_generated.cpp` – Generated protocol metadata
- `README.md` – Design documentation

### Core Types & Public API

**[mcp_client.h]**

```cpp
struct ServerConfig {
    std::string id;
    std::string type;                // "http", "sse", or "stdio"
    nlohmann::json raw;              // Raw config from mcp.json
    std::vector<std::string> diagnostics;
    bool supported = false;
};

struct ToolDescriptor {
    std::string server_id;           // Server ID ("docs", "weather", etc.)
    std::string name;                // Tool name from MCP server
    std::string local_name;          // Qualified name: "server_id.name"
    std::string title;
    std::string description;
    nlohmann::json input_schema;     // JSON schema for inputs
    nlohmann::json output_schema;
    nlohmann::json annotations;
};

struct ToolResult {
    std::string tool_name;           // "server_id.tool_name"
    std::string content;             // Normalized text result
    bool success = false;
    nlohmann::json metadata;         // Contains "raw" MCP response, "server", "mcp_tool", "error"
};

class Client {
  public:
    explicit Client(ClientOptions options);
    [[nodiscard]] Config config() const;
    [[nodiscard]] nlohmann::json diagnose_servers();
    [[nodiscard]] std::vector<ToolDescriptor> list_tools(const std::string &server_id);
    [[nodiscard]] ToolResult call_tool(const std::string &server_id, const std::string &tool_name,
                                       const nlohmann::json &arguments);
  private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};
```

### Initialize Negotiation Pattern

**[mcp_client.cpp, lines 734–769]**

The `Client::Impl::ensure_session()` method creates a transport and negotiates protocol version:

```cpp
[[nodiscard]] Session &ensure_session(const std::string &server_id)
{
    if (auto found = sessions_.find(server_id); found != sessions_.end())
    {
        return found->second;
    }

    const auto &server = server_config(server_id);
    Session session;
    
    if (server.type == "http" || server.type == "sse") {
        auto transport = std::make_unique<HttpTransport>(...);
        
        // Send initialize request with client capabilities
        const auto initialize = response_result(transport->request(make_request(
            next_id_++, "initialize",
            nlohmann::json{
                {"protocolVersion", std::string(registry_->latest_protocol_version())},
                {"capabilities", nlohmann::json::object()},
                {"clientInfo", nlohmann::json{{"name", "yaaf"}, {"version", "0.1.0"}}}
            })));
        
        // Extract negotiated protocol version and server info
        const auto protocol_version = as_string(
            initialize.value("protocolVersion", nlohmann::json{}), 
            registry_->latest_protocol_version());
        
        session.schema_backend = registry_->backend(protocol_version);
        if (session.schema_backend == nullptr) {
            throw std::runtime_error(fmt::format("unsupported MCP protocol version: {}", 
                                                   protocol_version));
        }
        
        session.protocol_version = protocol_version;
        session.server_info = initialize.value("serverInfo", nlohmann::json::object());
        transport->set_protocol_version(protocol_version);
        
        // Send initialized notification
        transport->notify(make_notification("notifications/initialized"));
        session.transport = std::move(transport);
    }
    else if (server.type == "stdio") {
        session.transport = std::make_unique<StdioTransport>(server.raw, 
                                                             options_.stdio_process_factory);
        // [Same initialization flow as HTTP, but using stdio transport]
    }

    auto [inserted, _] = sessions_.emplace(server_id, std::move(session));
    return inserted->second;
}
```

### Tool Discovery & Execution

**Tool Listing with Pagination** — `list_tools()` (lines 693–722):

```cpp
[[nodiscard]] std::vector<ToolDescriptor> list_tools(const std::string &server_id)
{
    auto &session = ensure_session(server_id);
    require_method(session, "tools/list");  // Gate on schema support
    
    std::vector<ToolDescriptor> tools;
    std::optional<std::string> cursor;
    
    do {
        nlohmann::json params = nlohmann::json::object();
        if (cursor.has_value()) {
            params["cursor"] = *cursor;
        }
        
        const auto result = response_result(
            session.transport->request(
                make_request(next_id_++, "tools/list", params)));
        
        if (const auto entries = result.find("tools"); 
            entries != result.end() && entries->is_array())
        {
            for (const auto &entry : *entries) {
                if (!entry.is_object() || !entry.contains("name")) continue;
                
                ToolDescriptor tool;
                tool.server_id = server_id;
                tool.name = entry.at("name").get<std::string>();
                tool.local_name = server_id + "." + tool.name;
                tool.title = as_string(entry.value("title", nlohmann::json{}));
                tool.description = as_string(entry.value("description", nlohmann::json{}));
                tool.input_schema = entry.value("inputSchema", nlohmann::json::object());
                tool.output_schema = entry.value("outputSchema", nlohmann::json::object());
                tool.annotations = entry.value("annotations", nlohmann::json::object());
                tools.push_back(std::move(tool));
            }
        }
        
        cursor.reset();
        if (auto next = result.find("nextCursor"); 
            next != result.end() && next->is_string() && !next->empty())
        {
            cursor = next->get<std::string>();
        }
    } while (cursor.has_value());
    
    return tools;
}
```

**Tool Execution** — `call_tool()` (lines 724–751):

```cpp
[[nodiscard]] ToolResult call_tool(const std::string &server_id, 
                                    const std::string &tool_name,
                                    const nlohmann::json &arguments)
{
    auto &session = ensure_session(server_id);
    require_method(session, "tools/call");
    
    ToolResult result;
    result.tool_name = server_id + "." + tool_name;
    result.metadata = {{"server", server_id}, {"mcp_tool", tool_name}};

    try {
        const auto response = response_result(
            session.transport->request(
                make_request(next_id_++, "tools/call", 
                    nlohmann::json{
                        {"name", tool_name}, 
                        {"arguments", arguments}
                    })));
        
        result.success = !response.value("isError", false);
        result.content = content_to_text(response);  // Normalize result
        result.metadata["raw"] = response;
    }
    catch (const std::exception &error) {
        result.success = false;
        result.content = fmt::format("MCP tool failed: {}", error.what());
        result.metadata["error"] = error.what();
    }
    
    return result;
}
```

### Result Normalization

**`content_to_text()`** (lines 445–465):

```cpp
[[nodiscard]] std::string content_to_text(const nlohmann::json &result)
{
    std::vector<std::string> parts;
    
    // Prefer MCP standard: extract text from content array
    if (const auto content = result.find("content"); 
        content != result.end() && content->is_array())
    {
        for (const auto &entry : *content) {
            const auto type = entry.find("type");
            if (type != entry.end() && type->is_string() && *type == "text" && 
                entry.contains("text") && entry.at("text").is_string())
            {
                parts.push_back(entry.at("text").get<std::string>());
            }
            else if (entry.is_object()) {
                parts.push_back(entry.dump());
            }
        }
    }
    
    // Fallback: use structuredContent if no text content found
    if (parts.empty() && result.contains("structuredContent")) {
        return result.at("structuredContent").dump();
    }
    
    std::string joined;
    for (const auto &part : parts) {
        if (!joined.empty()) joined += '\n';
        joined += part;
    }
    return joined;
}
```

### Transport Layer

**HttpTransport** (lines 239–334):
- Sends `Accept: application/json, text/event-stream`
- Handles SSE (`text/event-stream`) response parsing
- Maintains `Mcp-Session-Id` and `MCP-Protocol-Version` headers
- Throws on non-2xx status codes

**StdioTransport** (lines 336–359):
- Writes newline-delimited JSON to subprocess stdin
- Reads messages from subprocess stdout
- Matches response `id` to request `id` for request–response pairing
- 30-second timeout on `read_message()`

---

## 2. Existing Lua MCP Module (`libyaaf/script/modules/script_mcp.cpp`)

### Lua-Facing API

**[script_mcp.cpp, lines 171–180]**

```cpp
void register_mcp_module(lua_State *state, ScriptMcpContext &context)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "preload");
    push_mcp_function(state, context, open_mcp_module);
    lua_setfield(state, -2, "mcp");
    lua_pop(state, 2);
}

int open_mcp_module(lua_State *state)
{
    auto &runtime = context(state);
    lua_newtable(state);

    push_mcp_function(state, runtime, lua_config);
    lua_setfield(state, -2, "config");
    
    push_mcp_function(state, runtime, lua_servers);
    lua_setfield(state, -2, "servers");
    
    push_mcp_function(state, runtime, lua_diagnostics);
    lua_setfield(state, -2, "diagnostics");
    
    push_mcp_function(state, runtime, lua_list_tools);
    lua_setfield(state, -2, "list_tools");
    
    push_mcp_function(state, runtime, lua_call_tool);
    lua_setfield(state, -2, "call_tool");

    return 1;
}
```

### Exported Functions

**`mcp.config()`** – Returns configuration object
```lua
local config = require("mcp").config()
-- Result: {path = "...", exists = true, servers = {...}, diagnostics = {...}}
```

**`mcp.servers()`** – List all servers
```lua
local servers = require("mcp").servers()
-- Result: array of {id, type, supported, diagnostics}
```

**`mcp.diagnostics()`** – Initialize all servers and return diagnostics
```lua
local diags = require("mcp").diagnostics()
-- Result: array with initialize/tools status per server
```

**`mcp.list_tools(server_id)`** – List tools from a server
```lua
local tools = require("mcp").list_tools("weather")
-- Result: array of tool descriptors {
--   server_id, name, local_name, title, description,
--   inputSchema, parameters, outputSchema, annotations
-- }
```

**`mcp.call_tool(server_id, tool_name, arguments)`** – Execute a tool
```lua
local result = require("mcp").call_tool("weather", "get_forecast", {location = "NYC"})
-- Result: {tool_name, content, success, metadata}
```

### Context & Initialization

**[lua_runtime.cpp, lines 261–280]**

```cpp
ScriptMcpContext mcp_context;
mcp_context.options.workspace_root = 
    options.workspace_root.empty() ? previous_path : options.workspace_root;
mcp_context.options.config_path = options.mcp_config_path;
mcp_context.options.http = options.http;

if (services != nullptr && services->mcp_http_post) {
    mcp_context.options.http_post = services->mcp_http_post;
}
if (services != nullptr && services->mcp_stdio_process_factory) {
    mcp_context.options.stdio_process_factory = services->mcp_stdio_process_factory;
}
if (services != nullptr && services->mcp_schema_registry != nullptr) {
    mcp_context.options.schema_registry = services->mcp_schema_registry;
}

modules::register_mcp_module(state, mcp_context);
```

---

## 3. Tool Registry & Execution (`libyaaf/script/modules/tool.cpp`)

### Tool Table Structure

All tools, regardless of source, present a uniform Lua interface:

```lua
local tool = {
  spec = {
    name = "weather",
    description = "Get weather forecast",
    parameters = { type = "object", properties = {...} }
  },
  provider = {
    type = "echo" | "lua" | "mcp",
    server = "weather",           -- for MCP tools
    tool = "get_forecast"         -- for MCP tools
  },
  execute = function(arguments) ... end
}
```

### Tool Sources & Lookup

**[tool.cpp, lines 325–345]**

Three sources merged into unified registry:

```cpp
[[nodiscard]] bool push_tool(lua_State *state, int custom_index, const std::string &name)
{
    // 1. Built-in echo tool
    if (name == "echo") {
        require_module(state, "tools.echo");
        return true;
    }
    
    // 2. Custom Lua-registered tools
    if (push_custom_tool(state, custom_index, name)) {
        return true;
    }
    
    // 3. MCP remote tools (via mcp.list_tools)
    if (auto mcp_tool = find_mcp_tool(state, name)) {
        push_mcp_tool(state, *mcp_tool);
        return true;
    }
    
    return false;
}

[[nodiscard]] std::vector<std::string> all_names(lua_State *state, int custom_index)
{
    std::vector<std::string> names;
    names.emplace_back("echo");  // Built-in

    // Add custom Lua tools
    lua_pushnil(state);
    while (lua_next(state, custom_index) != 0) {
        if (lua_type(state, -2) == LUA_TSTRING) {
            names.emplace_back(lua_tostring(state, -2));
        }
        lua_pop(state, 1);
    }

    // Add MCP tools with "server.tool" naming
    for (const auto &tool : list_mcp_tools(state)) {
        names.push_back(tool.name);  // e.g., "weather.forecast"
    }

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}
```

### MCP Tool Integration

**`list_mcp_tools()`** (tool.cpp, lines 63–154):
- Calls `mcp.servers()` to find supported servers
- Iterates supported servers, calls `mcp.list_tools(server_id)`
- Builds local `tool.name` as `"server_id.tool_name"`

**`push_mcp_tool()`** (tool.cpp, lines 177–203):
```cpp
void push_mcp_tool(lua_State *state, const McpTool &tool)
{
    lua_newtable(state);

    // spec table
    lua_newtable(state);
    lua_pushlstring(state, tool.name.c_str(), tool.name.size());
    lua_setfield(state, -2, "name");
    lua_pushlstring(state, tool.description.c_str(), tool.description.size());
    lua_setfield(state, -2, "description");
    push_json(state, tool.parameters);
    lua_setfield(state, -2, "parameters");
    lua_setfield(state, -2, "spec");

    // provider table
    lua_newtable(state);
    lua_pushstring(state, "mcp");
    lua_setfield(state, -2, "type");
    lua_pushlstring(state, tool.server_id.c_str(), tool.server_id.size());
    lua_setfield(state, -2, "server");
    lua_pushlstring(state, tool.tool_name.c_str(), tool.tool_name.size());
    lua_setfield(state, -2, "tool");
    lua_setfield(state, -2, "provider");

    // execute closure captures server_id and tool_name
    lua_pushlstring(state, tool.server_id.c_str(), tool.server_id.size());
    lua_pushlstring(state, tool.tool_name.c_str(), tool.tool_name.size());
    lua_pushcclosure(state, lua_mcp_execute, 2);
    lua_setfield(state, -2, "execute");
}

int lua_mcp_execute(lua_State *state)
{
    const char *server_id = luaL_checkstring(state, lua_upvalueindex(1));
    const char *tool_name = luaL_checkstring(state, lua_upvalueindex(2));
    const auto arguments = lua_isnoneornil(state, 1) 
        ? nlohmann::json::object() 
        : lua_to_json(state, 1);

    // Dispatch to mcp.call_tool()
    require_module(state, "mcp");
    lua_getfield(state, -1, "call_tool");
    lua_pushstring(state, server_id);
    lua_pushstring(state, tool_name);
    push_json(state, arguments);
    
    if (lua_pcall(state, 3, 1, 0) != 0) {
        auto message = lua_error_message(state);
        lua_pop(state, 1);
        throw std::runtime_error(message);
    }
    
    lua_remove(state, -2);
    return 1;
}
```

### Tool Registration & Selection

**`require("tool")`** Module API:

```lua
local tool = require("tool")

-- Register custom tool
tool.register({
  spec = {name = "my_tool", description = "...", parameters = {...}},
  execute = function(args) return {...} end
})

-- Get all available tool names
local names = tool.names()  -- {"echo", "my_tool", "docs.lookup", ...}

-- Select and list specs
local specs = tool.specs({"echo", "docs.lookup"})

-- Execute tool from selection
local result = tool.execute({"echo", "docs.lookup"}, "docs.lookup", {query = "..."})

-- Get provider metadata
local providers = tool.providers()
```

---

## 4. Schema & Metadata (`libyaaf/mcp/mcp_schema_generated.h/cpp`)

### Registry Hierarchy

**[mcp_schema.h]**

```cpp
struct VersionInfo {
    std::string_view version;            // e.g., "2025-11-25"
    std::string_view schema_url;
    std::string_view schema_path;
    std::size_t definition_count;
    std::size_t method_count;
};

struct MethodInfo {
    std::string_view method;              // e.g., "tools/list"
    std::string_view definition;          // "ListToolsRequest"
    MessageKind kind;                     // request or notification
};

class Backend {
  public:
    virtual ~Backend() = default;
    [[nodiscard]] virtual const VersionInfo &info() const = 0;
    [[nodiscard]] virtual const std::vector<MethodInfo> &methods() const = 0;
    [[nodiscard]] virtual const std::vector<std::string_view> &definitions() const = 0;
    [[nodiscard]] virtual bool has_definition(std::string_view definition) const = 0;
    [[nodiscard]] virtual std::optional<MethodInfo> method(std::string_view method) const = 0;
};

class Registry {
  public:
    virtual ~Registry() = default;
    [[nodiscard]] virtual std::string_view latest_protocol_version() const = 0;
    [[nodiscard]] virtual const std::vector<VersionInfo> &supported_versions() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const Backend> backend(std::string_view version) const = 0;
    [[nodiscard]] virtual bool is_supported_protocol_version(std::string_view version) const = 0;
};
```

### Generated Backends

**[mcp_schema_generated.h]**

```cpp
namespace yaaf::mcp::schema {

class GeneratedBackendFactory final : public BackendFactory {
  public:
    [[nodiscard]] std::shared_ptr<const Backend> create(std::string_view version) const override;
    [[nodiscard]] std::shared_ptr<const Backend> create_latest() const override;
    [[nodiscard]] std::shared_ptr<const Registry> create_registry() const override;
};

// Generated per MCP version in mcp/schema/
[[nodiscard]] std::shared_ptr<const Backend> generated_backend_2024_11_05();
[[nodiscard]] std::shared_ptr<const Backend> generated_backend_2025_03_26();
[[nodiscard]] std::shared_ptr<const Backend> generated_backend_2025_06_18();
[[nodiscard]] std::shared_ptr<const Backend> generated_backend_2025_11_25();
}
```

### Usage in Client

The `Client::Impl` gates method calls on schema support:

```cpp
static void require_method(const Session &session, std::string_view method)
{
    if (session.schema_backend == nullptr || 
        !session.schema_backend->method(method).has_value())
    {
        const auto version = session.schema_backend != nullptr 
            ? session.schema_backend->info().version 
            : "unknown";
        throw std::runtime_error(
            fmt::format("MCP protocol {} does not define method {}", version, method));
    }
}
```

**Protocol negotiation** selects backend:
```cpp
const auto protocol_version = as_string(
    initialize.value("protocolVersion", nlohmann::json{}), 
    registry_->latest_protocol_version());

session.schema_backend = registry_->backend(protocol_version);
if (session.schema_backend == nullptr) {
    throw std::runtime_error(
        fmt::format("unsupported MCP protocol version: {}", protocol_version));
}
```

---

## 5. Lua Runtime Setup (`libyaaf/script/lua_runtime.cpp`)

### Module Registration Order

**[lua_runtime.cpp, lines 240–285]**

```cpp
int run_file_impl(const LuaRuntimeOptions &options, const Services *services, nlohmann::json *command_metadata)
{
    lua_State *state = luaL_newstate();
    luaL_openlibs(state);

    // 1. Customize print()
    RuntimeOutputContext print_context;
    print_context.output = options.output;
    register_print(state, print_context);

    // 2. Set up package.path with precedence:
    //    - Script directory (highest priority)
    //    - Script grandparent (for module-style layouts)
    //    - Bundled lua/ directory next to executable (lowest priority, enables require("yaaf"))
    const auto runtime_root = 
        options.runtime_root.empty() 
            ? yaaf::platform::executable_directory() 
            : options.runtime_root;
    
    if (!runtime_root.empty()) {
        prepend_package_path(state, runtime_root / "lua");
    }
    prepend_package_path(state, absolute_path.parent_path().parent_path());
    prepend_package_path(state, absolute_path.parent_path());

    // 3. Register built-in native modules (in order)
    modules::register_json_module(state);

    ScriptHttpContext http_context;
    http_context.http = options.http;
    http_context.services = services;
    modules::register_http_module(state, http_context);

    ScriptLlmContext llm_context;
    llm_context.default_endpoint = options.endpoint;
    llm_context.default_model = options.model;
    llm_context.http = options.http;
    llm_context.services = services;
    modules::register_llm_module(state, llm_context);

    AgentContext agent_context;
    agent_context.default_endpoint = options.endpoint;
    agent_context.default_model = options.model;
    agent_context.http = options.http;
    agent_context.services = services;
    agent_context.output = options.output;
    modules::register_agent_module(state, agent_context);

    ScriptMcpContext mcp_context;
    mcp_context.options.workspace_root = 
        options.workspace_root.empty() ? previous_path : options.workspace_root;
    mcp_context.options.config_path = options.mcp_config_path;
    mcp_context.options.http = options.http;
    // [Populate mcp_context with services...]
    modules::register_mcp_module(state, mcp_context);

    modules::register_tool_module(state);

    ScriptYaafContext yaaf_context;
    yaaf_context.arguments = options.arguments;
    yaaf_context.default_endpoint = options.endpoint;
    yaaf_context.default_model = options.model;
    yaaf_context.options = options.options;
    yaaf_context.positionals = options.positionals;
    yaaf_context.command_metadata = command_metadata;
    yaaf_context.input = options.input;
    yaaf_context.output = options.output;
    modules::register_yaaf_module(state, yaaf_context);

    // 4. Load and execute the script
    const auto file_name = absolute_path.string();
    const int stack_top = lua_gettop(state);
    if (luaL_dofile(state, file_name.c_str()) != kLuaOk) {
        const char *message = lua_tostring(state, -1);
        throw std::runtime_error(
            fmt::format("Lua script failed: {}", 
                        message != nullptr ? message : "unknown error"));
    }

    if (command_metadata == nullptr && lua_gettop(state) > stack_top) {
        run_returned_command(state, stack_top + 1);
    }

    return EXIT_SUCCESS;
}
```

### Module Closure Pattern

Each module is registered with a context stored as an upvalue:

```cpp
void push_mcp_function(lua_State *state, ScriptMcpContext &runtime, lua_CFunction function)
{
    lua_pushlightuserdata(state, &runtime);
    lua_pushcclosure(state, function, 1);
}

// Inside lua_call_tool:
[[nodiscard]] ScriptMcpContext &context(lua_State *state)
{
    return *static_cast<ScriptMcpContext *>(lua_touserdata(state, lua_upvalueindex(1)));
}
```

---

## 6. Stdio Transport Implementation (`libyaaf/mcp/mcp_client_stdio.posix.cpp`)

### Process Management & Pipes

**[mcp_client_stdio.posix.cpp, lines 113–260]**

```cpp
class PosixStdioProcess final : public StdioPlatformProcess {
  public:
    explicit PosixStdioProcess(const nlohmann::json &raw) {
        const auto command = json_string_value(raw, "command");
        if (command.empty()) {
            throw std::runtime_error("stdio MCP server requires command");
        }

        // Build argv with optional "args" array from config
        std::vector<std::string> argv_storage;
        argv_storage.push_back(command);
        if (const auto args = raw.find("args"); args != raw.end() && args->is_array()) {
            for (const auto &arg : *args) {
                if (arg.is_string()) {
                    argv_storage.push_back(arg.get<std::string>());
                }
            }
        }

        // Build environment with overrides from envFile and env
        auto environment_storage = build_environment(raw);

        // Create pipes for stdin/stdout
        int stdout_pipe[2] = {-1, -1};
        int stdin_pipe[2] = {-1, -1};
        if (pipe(stdout_pipe) != 0 || pipe(stdin_pipe) != 0) {
            // cleanup and throw
        }

        FdGuard stdout_read{stdout_pipe[0]};
        FdGuard stdout_write{stdout_pipe[1]};
        FdGuard stdin_read{stdin_pipe[0]};
        FdGuard stdin_write{stdin_pipe[1]};

        // Set up file actions for posix_spawn
        posix_spawn_file_actions_t file_actions{};
        posix_spawn_file_actions_init(&file_actions);
        posix_spawn_file_actions_adddup2(&file_actions, stdin_read.get(), STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&file_actions, stdout_write.get(), STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&file_actions, stdin_write.get());
        // [Collect and close other fds]

        // Spawn process
        pid_t pid = 0;
        if (posix_spawn(&pid, command.c_str(), &file_actions, nullptr, argv, environment) != 0) {
            throw std::runtime_error("failed to spawn MCP stdio server");
        }

        // Close write end of stdout in parent, read end of stdin in parent
        stdout_write.reset();
        stdin_read.reset();

        input_fd_ = stdout_read.release();
        output_fd_ = stdin_write.release();
        process_ = pid;
    }

    void write_message(std::string_view line) override {
        // Write line + newline to stdin
    }

    [[nodiscard]] nlohmann::json read_message(std::chrono::milliseconds timeout) override {
        // Poll input_fd with timeout, read until newline, parse JSON
    }
};
```

### JSON-RPC Framing

**[mcp_client_stdio.cpp via StdioTransport]**

```cpp
void write_message(const nlohmann::json &message) {
    process_->write_message(message.dump() + "\n");
}

[[nodiscard]] nlohmann::json read_message() {
    return process_->read_message(std::chrono::seconds(30));
}

// Request–response pairing:
nlohmann::json request(const nlohmann::json &message) override {
    write_message(message);
    const auto expected_id = message.at("id");
    while (true) {
        auto response = read_message();
        if (response.contains("id") && response.at("id") == expected_id) {
            return response;
        }
    }
}
```

### Environment Variable Handling

**`read_environment_overrides()`** (mcp_client_stdio.h, lines 36–66):
- Reads `.env` file in KEY=VALUE format, ignoring comments (`#`) and malformed lines
- Overlays `"env"` object from JSON config
- Merges with parent process environment

---

## 7. Testing Patterns

### Smallest Test File: `tests/mock/mcp_protocol_tests.cpp`

**Focus:** Mocked HTTP server, protocol negotiation, tool discovery with pagination, error mapping.

```cpp
TEST(McpProtocolMockTests, NativeClientPaginatesSseListsAndMapsToolErrors)
{
    const auto workspace = make_workspace("assistant_mcp_native_client_test");
    write_mcp_config(workspace, nlohmann::json{
        {"servers", {{"docs", {
            {"type", "http"},
            {"url", "https://example.test/mcp"},
            {"headers", {{"Authorization", "Bearer token"}}}
        }}}}
    });

    std::vector<nlohmann::json> requests;
    std::vector<yaaf::mcp::Headers> headers_seen;
    
    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.schema_registry = std::make_shared<TestSchemaRegistry>(
        std::make_shared<TestSchemaBackend>(
            "2030-01-01", 
            std::vector<yaaf::mcp::schema::MethodInfo>{
                {"tools/list", "ListToolsRequest"},
                {"tools/call", "CallToolRequest"}
            }));
    
    options.http_post = [&](std::string_view, std::string_view body, 
                            std::string_view, const yaaf::mcp::Headers &headers) {
        headers_seen.push_back(headers);
        const auto request = nlohmann::json::parse(body);
        requests.push_back(request);
        const auto method = request.at("method").get<std::string>();
        
        if (method == "initialize") {
            EXPECT_EQ(request.at("params").at("protocolVersion").get<std::string>(), "2030-01-01");
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["protocolVersion"] = "2030-01-01";
            payload["result"]["capabilities"]["tools"] = nlohmann::json::object();
            return sse_response(payload, {{"Mcp-Session-Id", "session-1"}});
        }
        
        if (method == "notifications/initialized") {
            return HttpClient::Response{202, "", ""};
        }
        
        if (method == "tools/list" && !request.at("params").contains("cursor")) {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["tools"] = nlohmann::json::array();
            payload["result"]["nextCursor"] = "next";  // Pagination!
            return json_response(payload);
        }
        
        if (method == "tools/list") {  // With cursor
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["tools"] = nlohmann::json::array({
                {{"name", "lookup"}, {"title", "Lookup"}, {"description", "Look up docs"}}
            });
            return json_response(payload);
        }
        
        if (method == "tools/call") {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["content"] = nlohmann::json::array({
                {{"type", "text"}, {"text", "bad input"}}
            });
            payload["result"]["structuredContent"] = {{"code", "bad"}};
            payload["result"]["isError"] = true;
            return json_response(payload);
        }
        
        return HttpClient::Response{500, "text/plain", "unexpected"};
    };

    yaaf::mcp::Client client{options};
    
    const auto tools = client.list_tools("docs");
    ASSERT_EQ(tools.size(), 1U);
    EXPECT_EQ(tools.front().local_name, "docs.lookup");

    const auto result = client.call_tool("docs", "lookup", nlohmann::json{{"query", "mcp"}});
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.content, "bad input");
    EXPECT_EQ(result.metadata.at("raw").at("structuredContent").at("code"), "bad");
}
```

### Integration Test: `tests/integration/mcp/mcp_stdio_client_tests.cpp`

**Focus:** Real stdio subprocess, Lua integration, CLI flag handling.

```cpp
TEST(McpStdioClientIntegrationTests, NativeClientListsAndCallsScriptedStdioServer)
{
    const auto workspace = make_workspace("assistant_mcp_real_stdio_test");
    write_mcp_config(workspace, nlohmann::json{
        {"servers", {{"hello", scripted_stdio_server_config()}}}
    });

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.stdio_process_factory = scripted_stdio_process_factory();
    
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}

TEST(McpStdioClientIntegrationTests, LuaMcpModuleUsesExplicitMcpConfigPath)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_lua_direct_module_test");
    write_mcp_config(workspace, nlohmann::json{
        {"servers", {{"hello", scripted_stdio_server_config()}}}
    });
    
    const auto script_path = write_lua_script(workspace, R"lua(
local mcp = require("mcp")
local result = mcp.call_tool("hello", "repeat", { text = "Lua", count = 2 })
print(result.content)
)lua");
    
    const CurrentPathGuard current_path{root};

    yaaf::cli::Services services;
    services.mcp_stdio_process_factory = scripted_stdio_process_factory();

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run(
        {"run", "--mcp", (workspace_mcp_config_path(workspace)).string(), script_path.string()},
        input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "Lua Lua\n");
}

TEST(McpStdioClientIntegrationTests, LuaScriptUsesExplicitMcpConfigPath)
{
    // ... Similar test using tool.execute() instead of mcp.call_tool()
}
```

### Test Support Infrastructure

**[tests/support/mcp_test_support.h]**
- `make_workspace()` – Create temp directory with isolation
- `write_mcp_config()` – Serialize mcp.json
- `scripted_stdio_server_config()` – Return config for test subprocess
- `scripted_stdio_process_factory()` – Factory that spawns test MCP servers
- `expect_hello_tools()` – Assertion helper

---

## Key Design Patterns for Host Bridge

### 1. **Protocol Negotiation**
- Send `initialize` with `protocolVersion` and capabilities
- Select schema backend from negotiated version
- Gate all method calls on schema support via `require_method()`
- Send `notifications/initialized`

### 2. **Request–Response Pairing**
- Each request has integer `id`
- Match response `id` to route to correct caller
- Timeout waiting for response (30 seconds for stdio)

### 3. **Pagination**
- Request parameters include optional `cursor`
- Response includes `nextCursor` if more data exists
- Loop until cursor is absent

### 4. **Result Normalization**
- Extract text from `content` array (preferred)
- Fall back to `structuredContent` if no text
- Always capture raw response in `metadata`

### 5. **Lua Module Registration**
- Use upvalue closure pattern to capture context
- Store context as `lightuserdata` in closure
- Register in `package.preload` for lazy `require()`

### 6. **Tool Integration**
- Merge multiple sources (built-in, Lua, MCP) into single registry
- Each tool has `spec` (metadata) and `execute` (function)
- MCP tools created with `"server"` and `"tool"` closure captures

### 7. **Error Handling**
- MCP errors: `response["error"]["message"]`
- Protocol errors: empty body, non-2xx HTTP status, malformed JSON
- Lua errors: throw from C++ functions, caught and formatted in Lua

---

## Summary

The yaaf codebase is well-structured for both MCP client consumption and Lua scripting:

1. **Native MCP client** cleanly separates transport (HTTP/SSE/stdio) from protocol logic
2. **Generated schema** provides protocol version and method support metadata
3. **Lua bridge** maintains thin abstraction while exposing native capabilities
4. **Tool registry** elegantly merges echo, custom, and MCP tools
5. **Stdio transport** uses newline-delimited JSON-RPC with request–response pairing
6. **Testing** combines mock (protocol behavior) and integration (real subprocess) patterns

The **host bridge** should mirror this design: native stdio/JSON-RPC handling, thin Lua bridge for tool/prompt dispatch, and reuse of the existing tool execution and schema infrastructure.
