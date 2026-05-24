local yaaf = require("yaaf")
local json = require("json")
local agent = require("agent")
local tool = require("tool")
local mcp = require("mcp")

local function collect_tools()
    local result = {}
    for _, registered_tool in ipairs(tool.create_many(tool.names())) do
        local spec = registered_tool.spec or {}
        table.insert(result, {
            name = spec.name or "",
            description = spec.description or "",
            parameters = spec.parameters or {},
        })
    end
    return result
end

local function collect_agents()
    local result = {}
    for _, name in ipairs(agent.names()) do
        table.insert(result, { name = name })
    end
    return result
end

local function collect_mcp_report()
    local report = mcp.config()
    local active_by_id = {}
    for _, diagnostic in ipairs(mcp.diagnostics()) do
        active_by_id[diagnostic.id] = diagnostic
    end
    for _, server in ipairs(report.servers or {}) do
        server.active = active_by_id[server.id]
    end
    return report
end

local function collect_report()
    return {
        environment = {
            lua = _VERSION,
            endpoint = yaaf.defaults.endpoint,
            model = yaaf.defaults.model,
        },
        registries = {
            agents = collect_agents(),
            tools = collect_tools(),
            tool_providers = tool.providers(),
        },
        mcp = collect_mcp_report(),
        app_modes = {
            ask = { tools = true },
            chat = { tools = true },
            agent = { tools = true },
            embed = { tools = false },
        },
    }
end

local function format_initialize_status(server)
    local active = server.active or {}
    local initialize = active.initialize or {}
    if initialize.status == "ok" then
        local protocol_version = initialize.protocol_version or ""
        if protocol_version ~= "" then
            return "ok (protocol " .. protocol_version .. ")"
        end
        return "ok"
    end
    local error_message = initialize.error or "active MCP initialization failed"
    return "failed - " .. error_message
end

local function format_tool_status(server)
    local active = server.active or {}
    local tools = active.tools or {}
    if tools.status == "ok" then
        local names = tools.names or {}
        if #names == 0 then
            return tostring(tools.count or 0) .. " discovered"
        end
        return tostring(tools.count or #names) .. " discovered: " .. table.concat(names, ", ")
    end
    local error_message = tools.error or "MCP tool discovery failed"
    return "failed - " .. error_message
end

local function print_text(report)
    print("yaaf doctor")
    print("environment:")
    print("  lua: " .. report.environment.lua)
    print("  endpoint: " .. report.environment.endpoint)
    print("  model: " .. report.environment.model)

    print("registered agents:")
    if #report.registries.agents == 0 then
        print("  none")
    else
        for _, agent in ipairs(report.registries.agents) do
            print("  - " .. agent.name)
        end
    end

    print("registered tools:")
    if #report.registries.tools == 0 then
        print("  none")
    else
        for _, tool in ipairs(report.registries.tools) do
            print("  - " .. tool.name .. ": " .. tool.description)
            print("    parameters: " .. json.encode(tool.parameters))
        end
    end

    print("mcp:")
    print("  config: " .. report.mcp.path)
    if not report.mcp.exists then
        print("  status: not configured")
    elseif #report.mcp.servers == 0 then
        print("  servers: none")
    else
        print("  servers:")
        for _, server in ipairs(report.mcp.servers) do
            local status = server.supported and "supported" or "unsupported"
            print("  - " .. server.id .. " (" .. server.type .. ", " .. status .. ")")
            for _, diagnostic in ipairs(server.diagnostics or {}) do
                print("    warning: " .. diagnostic)
            end
            print("    initialize: " .. format_initialize_status(server))
            print("    tools: " .. format_tool_status(server))
        end
    end

    print("app modes:")
    print("  ask: tools enabled")
    print("  chat: tools enabled")
    print("  agent: tools enabled")
    print("  embed: tools disabled")
end

local function run(command)
    local options = command.options
    local report = collect_report()
    if options.format == "json" then
        print(json.encode(report, options.pretty))
        return
    end

    if options.format ~= nil and options.format ~= "" then
        error("doctor only supports --format json")
    end

    print_text(report)
end

return yaaf.command({
    description = "Print yaaf environment and Lua registry information",
    options = {
        {
            name = "format",
            flags = { "--format" },
            type = "string",
            description = "Output format for doctor; accepts json",
        },
        {
            name = "pretty",
            flags = { "--pretty" },
            type = "flag",
            description = "Pretty-print JSON output",
        },
    },
    run = run,
})
