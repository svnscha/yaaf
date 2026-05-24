local yaaf = require("yaaf")
local agent_registry = require("agent")

local available_agents = table.concat(agent_registry.names(), ", ")

local function run(command)
    local options = command.options
    local agent = agent_registry.create(options.name, {
        provider = options.provider,
        endpoint = options.endpoint,
        model = options.model,
        think = options.think,
        max_turns = tonumber(options.max_turns),
        tools = options.tool,
    })
    agent:run(command.positionals.prompt)
end

return yaaf.command({
    description = "Run a native agent against the selected model endpoint",
    options = {
        {
            name = "name",
            flags = { "--name" },
            description = "Agent implementation to run; currently supported: " .. available_agents,
            required = true,
        },
        {
            name = "provider",
            flags = { "--provider" },
            description = "Provider used by this command",
            default = "ollama",
        },
        {
            name = "endpoint",
            flags = { "--endpoint" },
            description = "Endpoint used by this command",
            default = yaaf.defaults.endpoint,
        },
        {
            name = "model",
            flags = { "--model" },
            description = "Model used by this command",
            default = "ministral-3:14b",
        },
        {
            name = "think",
            flags = { "--think" },
            description = "Thinking level for this command: none, high, medium, or low",
            default = "none",
        },
        {
            name = "max_turns",
            flags = { "--max-turns" },
            description = "Maximum number of agent turns before aborting",
            default = "10",
        },
        {
            name = "tool",
            flags = { "--tool" },
            type = "strings",
            description = "Registered tool made available to the agent; may be passed multiple times",
        },
        {
            name = "mcp",
            flags = { "--mcp" },
            description = "MCP server configuration file used for this agent run",
        },
    },
    positionals = {
        {
            name = "prompt",
            description = "Prompt to send to the selected agent",
            required = true,
        },
    },
    run = run,
})
