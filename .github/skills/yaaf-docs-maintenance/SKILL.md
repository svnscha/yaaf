---
name: yaaf-docs-maintenance
description: 'Use when updating yaaf user documentation, README files, Lua API docs, tool reference docs, MCP docs, examples, build/run instructions, CLI option docs, or runtime behavior docs. Keeps README.md, docs/, docs/modules/, docs/tools/, libyaaf/mcp/README.md, and MCP_PLAN.md aligned with implementation and tests.'
argument-hint: 'What behavior or documentation changed?'
---

# Yaaf Docs Maintenance

Use this skill when changing or reviewing user-facing documentation for yaaf, especially after edits to CLI behavior, Lua runtime modules, MCP support, shipped commands, build outputs, examples, environment variables, or fixture servers.

## Documentation Map

Keep each document focused on its job:

- [Root README](https://github.com/svnscha/yaaf#readme): concise project overview, build command, documentation links, and quick smoke tests.
- [Docs index](https://svnscha.github.io/yaaf/): table of contents for user-facing docs and MkDocs home page.
- [Usage](https://svnscha.github.io/yaaf/usage/): build output, executable assumptions, environment variables, core CLI commands, embeddings, and proxy setup.
- [Lua Runtime](https://svnscha.github.io/yaaf/lua/): direct script runs, command discovery, and short module overview.
- [Lua API Reference](https://svnscha.github.io/yaaf/modules/): per-module runtime API reference, with one page per public Lua module.
- [Tool Reference](https://svnscha.github.io/yaaf/tools/): built-in tool list, input schemas, MCP tool naming pointer, and custom tool authoring guide.
- [MCP Tools](https://svnscha.github.io/yaaf/mcp/): user-facing MCP config, transports, variable substitution, tool names, runtime behavior, schema support, and fixtures.
- [Examples](https://svnscha.github.io/yaaf/examples/): example overview, with one page per runnable example or scenario.
- [MCP support README](https://github.com/svnscha/yaaf/blob/main/libyaaf/mcp/README.md): implementation-level MCP support matrix.
- [MCP plan](https://github.com/svnscha/yaaf/blob/main/MCP_PLAN.md): architecture, rollout notes, and historical implementation plan.
- [Fixture README](https://github.com/svnscha/yaaf/blob/main/mcp-servers/README.md): hello-world MCP fixture setup and test-only fixture knobs.

## Source Of Truth

Before editing docs, inspect the implementation or tests for the behavior being documented.

CLI and runtime sources:

- [CLI entry point](../../../libyaaf/cli/cli.cpp)
- [Lua runtime](../../../libyaaf/script/lua_runtime.cpp)
- [Native Lua modules](../../../libyaaf/script/modules)
- [Shipped Lua commands](../../../lua/cli)
- [Shipped agents](../../../lua/agents)
- [Shipped tools](../../../lua/tools)

MCP sources:

- [Native MCP client](../../../libyaaf/mcp/mcp_client.cpp)
- [Native MCP API](../../../libyaaf/mcp/mcp_client.h)
- [MCP support README](../../../libyaaf/mcp/README.md)
- [MCP tests](../../../tests/plain/mcp_config_schema_tests.cpp)
- [Mocked MCP protocol tests](../../../tests/mock/mcp_protocol_tests.cpp)
- [Integration MCP client tests](../../../tests/integration/mcp_client_tests.cpp)

Build and validation sources:

- [Root CMake](../../../CMakeLists.txt)
- [App CMake](../../../app/CMakeLists.txt)
- [Test CMake](../../../tests/CMakeLists.txt)
- [Repository workflow](../../../AGENTS.md)

## Procedure

1. Identify the behavior that changed: CLI command, option, environment variable, build output, Lua module API, shipped command/tool/agent, MCP behavior, fixture behavior, or validation command.
2. Read the matching source files and tests before writing docs. Do not document aspirational behavior as current behavior.
3. Update the narrowest user-facing docs first, then update index links and the root README only when discoverability changes.
4. Keep implementation-level details in implementation docs. For MCP internals, prefer [MCP support README](https://github.com/svnscha/yaaf/blob/main/libyaaf/mcp/README.md); for user workflows, prefer [MCP Tools](https://svnscha.github.io/yaaf/mcp/) and [Examples](https://svnscha.github.io/yaaf/examples/).
5. If a change affects Lua modules, update the matching page under [Lua API Reference](https://svnscha.github.io/yaaf/modules/), add a new module page when a public module appears, and keep [Lua Runtime](https://svnscha.github.io/yaaf/lua/) as the shorter overview.
6. If a change affects built-in tools, tool result shape, tool registration, or custom tool authoring, update [Tool Reference](https://svnscha.github.io/yaaf/tools/) and link to the relevant Lua API page instead of duplicating registry details everywhere.
7. If a command example changes, search the docs for stale copies of the old invocation and update related example pages together.
8. If a feature is intentionally unsupported, say so plainly and avoid documenting partial or flaky behavior as supported.
9. Validate docs changes with `git diff --check`. For documentation generated from, or tightly coupled to, C++ behavior, also build the affected target or run the focused tests that prove the documented behavior.

## Documentation Rules

- Prefer short, copyable examples over long prose.
- Keep root README slim; move detailed usage into `docs/`.
- Use `yaaf.exe` in user examples when the build output directory is assumed to be on `PATH`.
- Mention the exact assumption for examples: repository root, executable directory, or build output directory on `PATH`.
- Keep environment variable precedence accurate: process environment first, then nearest parent `.env` where implemented.
- Keep MCP config path selection explicit only: `--mcp <path>` or `YAAF_MCP_FILE`; yaaf must not auto-discover editor-owned MCP config files outside `.yaaf/mcp.json`.
- Do not document `${input:...}` as supported; yaaf rejects it. Use `${env:NAME}` or `envFile` examples instead.
- Keep fixture-only environment variables out of normal user configuration docs. Document them only as test or fixture overrides.
- Document modules by public API name rather than implementation layer; users should not need to know whether a module is native C++ or shipped Lua.
- For built-in runtime modules, mention that they are built in during the first sentence of the module page, then focus on behavior and API.
- Keep Lua module docs aligned with functions actually returned by the module tables.
- Keep each public module on its own page under `docs/modules/`; avoid recreating a long single-page module reference.
- Keep [Tool Reference](https://svnscha.github.io/yaaf/tools/) focused on developer workflow: built-in tool names, schemas, custom tool shape, registration, and links to complete examples.
- When adding, renaming, or removing a built-in tool, update [Tool Reference](https://svnscha.github.io/yaaf/tools/), the matching module page if one exists, and `mkdocs.yml` when a new public page is added.
- When adding, renaming, or removing a module page, update `mkdocs.yml`, [Lua API Reference](https://svnscha.github.io/yaaf/modules/), [Docs index](https://svnscha.github.io/yaaf/) when discoverability changes, and [Root README](https://github.com/svnscha/yaaf#readme) only for top-level documentation entry changes.
- Keep each runnable example or scenario on its own page under `docs/examples/`; keep [Examples](https://svnscha.github.io/yaaf/examples/) as a short index.
- When adding, renaming, or removing an example page, update `mkdocs.yml`, [Examples](https://svnscha.github.io/yaaf/examples/), [Docs index](https://svnscha.github.io/yaaf/) when discoverability changes, and [Root README](https://github.com/svnscha/yaaf#readme) only for top-level documentation entry changes.
- Avoid duplicating full command references in multiple places. Put full module/API details in [Lua API Reference](https://svnscha.github.io/yaaf/modules/), tool authoring guidance in [Tool Reference](https://svnscha.github.io/yaaf/tools/), and copyable task examples in [Examples](https://svnscha.github.io/yaaf/examples/).

## Validation Checklist

For docs-only changes:

```powershell
git diff --check
.\.venv\Scripts\python.exe -m mkdocs build --strict
```

If the repository virtual environment is unavailable, use `python -m mkdocs build --strict` after installing `requirements-docs.txt`.

For build/run documentation changes:

```powershell
cmake --build build --config Debug --target yaaf
```

For CLI, Lua, or MCP behavior documentation that changed alongside code:

```powershell
cmake --build build --config Debug --target libyaaf_tests
.\build\tests\Debug\libyaaf_tests.exe
```

For MCP-specific behavior changes, also follow the [MCP protocol support skill](../mcp-protocol-support/SKILL.md) (`yaaf-mcp-protocol-support`).
