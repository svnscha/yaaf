# Lua API Reference

Yaaf modules are documented by API name, not by implementation location. Pages whose module is built into the runtime say so in the first sentence.

The `lua/` directory is still part of the runtime surface for built-in commands, agents, and tools, but scripts should depend on the public module APIs below rather than private helper functions inside command files.

For built-in tool names, schemas, and a custom tool authoring guide, see [Tool Reference](../tools/index.md).

## Runtime Modules

- [yaaf](yaaf.md): process, command metadata, stdin/stdout, and runtime defaults.
- [json](json.md): JSON encode/decode helpers.
- [http](http.md): low-level HTTP transport bridge.
- [llm](llm.md): provider-neutral generate, chat, and embed bridge.
- [mcp](mcp.md): MCP config reports, server discovery, tool listing, and tool calls.
- [agent](agent.md): agent registry and agent-authoring primitives.
- [tool](tool.md): built-in, script-registered, and MCP-discovered tool registry API.

## Agent And Tool Implementations

- [agents.react](agents-react.md): the included ReAct agent implementation.
- [tools.echo](tools-echo.md): the included deterministic echo tool.

## Command Modules

Files under `lua/cli/` become top-level CLI subcommands when `yaaf` starts and can discover the runtime `lua/` directory.

- `lua/cli/ask.lua`: `ask`, a one-shot prompt command. It supports model and endpoint overrides, streaming, thinking output, JSON or schema output, and tool calls through registered Lua or MCP tools.
- `lua/cli/chat.lua`: `chat`, an interactive chat command. It supports an optional first prompt, streaming, thinking output, and registered tools.
- `lua/cli/agent.lua`: `agent`, an agent runner. It selects an agent, configures model and turn limits, and passes selected tools to the agent.
- `lua/cli/embed.lua`: `embed`, an embeddings command for one or more inputs. It supports model and endpoint overrides, dimensionality overrides, truncation control, and JSON output.
- `lua/cli/doctor.lua`: `doctor`, a diagnostics command. It reports runtime defaults, registered agents, registered tools, MCP config state, and app modes.

The app target copies `lua/` and `examples/` beside `yaaf` after every build, so command modules and examples are available from the deployed layout.
