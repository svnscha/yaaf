# yaaf

[![CI](https://github.com/svnscha/yaaf/actions/workflows/ci.yml/badge.svg)](https://github.com/svnscha/yaaf/actions/workflows/ci.yml)
[![Docs](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://svnscha.github.io/yaaf/)
[![GitHub stars](https://img.shields.io/github/stars/svnscha/yaaf?style=social)](https://github.com/svnscha/yaaf/stargazers)
[![Platforms](https://img.shields.io/badge/platforms-windows%20%7C%20macOS%20%7C%20Linux-0a7ea4)](https://github.com/svnscha/yaaf/actions/workflows/ci.yml)

Yaaf is a command-line runtime for small AI workflows in Lua. You can use it as a normal CLI for prompts and chat, wire in local or MCP tools, or run a Lua script that registers its own tools and agents.

The native layer handles startup, HTTP, JSON, and MCP transports. Lua handles commands, tools, agents, and provider logic, so you can start with copyable commands and then grow into custom workflows without changing runtimes.

Current development and CI support cover Windows, macOS, and Ubuntu Linux. The first Linux package is built on Ubuntu and should be treated as an Ubuntu-targeted artifact rather than a universal package for every Linux distribution. The current runtime smoke matrix passes on Ubuntu 24.04 and records the expected glibc/libstdc++ compatibility failure on Ubuntu 22.04.

## What It Is Good At

- One-shot prompts from the terminal.
- Chat sessions with local or remote models.
- Tool-using agents with a small Lua surface area.
- MCP-backed tools loaded from a VS Code-style `mcp.json`.
- Direct Lua scripts that register local tools for a single run.

## Quick Start

These examples assume `yaaf` is already built and the build output directory is on `PATH`.

Ask a quick question:

```powershell
yaaf ask "Explain RAII in one sentence."
```

Ask through an OpenAI-compatible endpoint:

```powershell
$env:YAAF_OPENAI_API_KEY = "sk-example"
yaaf ask --provider openai --model gpt-4o-mini "Explain RAII in one sentence."
```

Open a short chat:

```powershell
yaaf chat "Reply with one short greeting."
```

Stream output as it arrives:

```powershell
yaaf ask --stream "Write a haiku about C++."
```

Request JSON output for automation:

```powershell
yaaf ask --format json --pretty "Return a JSON object with answer equal to 2."
```

Inspect your current runtime configuration:

```powershell
yaaf doctor --format json --pretty
```

## Simple Real-World Examples

Use a local script to add a tiny custom tool and run an agent:

```powershell
yaaf run ./examples/weather_agent.lua "Use the weather tool to tell me the weather in Berlin."
```

Use the built-in echo tool to verify agent tool wiring before involving external services:

```powershell
yaaf agent --name react --tool echo "Use the echo tool to repeat hello."
```

Point yaaf at an explicit MCP config and call a remote tool:

```powershell
yaaf ask --mcp ./.vscode/mcp.json --tool docs.lookup "Look up the install steps."
```

Run a Lua script directly when you want full control over the workflow:

```powershell
yaaf run ./examples/example.lua one two three
```

## Start Here

- [Usage](https://svnscha.github.io/yaaf/usage/): build output, environment variables, command reference, embeddings, and proxy setup.
- [Examples](https://svnscha.github.io/yaaf/examples/): copyable CLI, Lua, ReAct, and MCP examples.
- [Lua Runtime](https://svnscha.github.io/yaaf/lua/): how command modules and direct scripts are discovered and run.
- [Lua API Reference](https://svnscha.github.io/yaaf/modules/): built-in runtime modules such as `llm`, `tool`, `agent`, and `mcp`.
- [MCP Tools](https://svnscha.github.io/yaaf/mcp/): explicit MCP config paths, supported config shape, and tool naming.
- [Tool Reference](https://svnscha.github.io/yaaf/tools/): built-in tools and custom tool authoring.

## Build And Detailed Setup

Build, environment setup, executable locations, and command reference live in [Usage](https://svnscha.github.io/yaaf/usage/). The full documentation index is at [https://svnscha.github.io/yaaf/](https://svnscha.github.io/yaaf/).

Serve the docs locally with MkDocs:

```powershell
python -m pip install -r requirements-docs.txt
python -m mkdocs serve
```

Build the static docs site with:

```powershell
python -m mkdocs build --strict
```

Implementation-level MCP protocol support details are maintained in [libyaaf/mcp/README.md](libyaaf/mcp/README.md).
