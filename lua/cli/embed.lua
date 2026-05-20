local yaaf = require("yaaf")
local json = require("json")
local llm = require("llm")
local ollama = llm.create("ollama")

local function run(command)
    local options = command.options
    if options.format ~= nil and options.format ~= "" and options.format ~= "json" then
        error("embed only supports --format json")
    end

    local inputs = command.positionals.inputs
    local input = inputs
    if #inputs == 1 then
        input = inputs[1]
    end

    local request = {
        endpoint = options.endpoint,
        model = options.model,
        input = input,
        truncate = not options.no_truncate,
    }

    if options.dimensions ~= nil and options.dimensions ~= "" then
        request.dimensions = math.tointeger(options.dimensions)
        if request.dimensions == nil then
            error("--dimensions must be an integer for embed")
        end
    end

    local response = ollama.embed(request)
    print(json.encode({
        model = response.model,
        embeddings = response.embeddings,
        total_duration = response.total_duration,
        load_duration = response.load_duration,
        prompt_eval_count = response.prompt_eval_count,
    }, options.pretty))
end

return yaaf.command({
    description = "Generate embeddings for one or more input texts",
    options = {
        {
            name = "endpoint",
            flags = { "--endpoint" },
            type = "string",
            default = yaaf.defaults.endpoint,
            description = "Endpoint used by this command",
        },
        {
            name = "model",
            flags = { "--model" },
            type = "string",
            default = yaaf.defaults.model,
            description = "Embedding model used by this command",
        },
        {
            name = "format",
            flags = { "--format" },
            type = "string",
            description = "Output format for embed; accepts json",
        },
        {
            name = "dimensions",
            flags = { "--dimensions" },
            type = "string",
            description = "Optional embedding dimensionality override",
        },
        {
            name = "no_truncate",
            flags = { "--no-truncate" },
            type = "flag",
            description = "Disable input truncation for the embed command",
        },
        {
            name = "pretty",
            flags = { "--pretty" },
            type = "flag",
            description = "Pretty-print JSON output",
        },
    },
    positionals = {
        {
            name = "inputs",
            type = "string",
            multiple = true,
            required = true,
            description = "One or more inputs to embed",
        },
    },
    run = run,
})
