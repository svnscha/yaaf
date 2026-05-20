# agent

`agent` is a built-in module that exposes the agent registry and lower-level agent-authoring primitives.

Load it with:

```lua
local agent = require("agent")
```

## Registry Functions

- `agent.names()`: returns registered agent names.
- `agent.load(name)`: loads an agent implementation module.
- `agent.create(name, options)`: constructs an agent instance.

The registry currently contains `react`, implemented by [agents.react](agents-react.md).

```lua
local runner = agent.create("react", {
    model = "ministral-3:14b",
    max_turns = 6,
    tools = { "echo" },
})

runner:run("Use the echo tool to repeat hello.")
```

## Authoring Primitives

- `agent.complete_chat(request)`: non-streaming native chat completion used by agents.
- `agent.emit(event)`: writes normalized agent trace events.
- `agent.strip_think_tags(text)`: removes `<think>...</think>` blocks from model output.

Supported `emit` event kinds are `user`, `yaaf`, `final_answer`, `thought`, `tool_call`, and `tool_observation`.
