# agents.react

`agents.react` is the included ReAct agent implementation used by `yaaf agent --name react`.

Most scripts should create it through [agent](agent.md):

```lua
local agent = require("agent")

local runner = agent.create("react", {
    model = "ministral-3:14b",
    max_turns = 6,
    tools = { "echo" },
})
```

Load the implementation module directly when you are testing or extending agent behavior:

```lua
local react = require("agents.react")
```

## Functions

- `react.new(options)`: creates an agent.
- `react.parse_response(text)`: parses the expected ReAct JSON response shape.
- `react.response_schema()`: returns the JSON schema requested from the model.

`react.new` accepts `endpoint`, `model`, `think`, `max_turns`, and `tools`. The returned agent exposes `agent:run(input)`, which emits trace lines and returns `{ content, tool_results, turns, metadata }`.
