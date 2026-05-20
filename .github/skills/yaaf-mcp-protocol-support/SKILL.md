---
name: yaaf-mcp-protocol-support
description: 'Use when maintaining yaaf MCP protocol support documentation, the MCP support matrix, generated MCP schema files, MCP config behavior, transports, tool integration, or tests. Keeps libyaaf/mcp/README.md aligned with native MCP implementation and validation.'
argument-hint: 'What MCP support behavior changed?'
---

# MCP Protocol Support Maintenance

Use this skill when changing or reviewing yaaf's MCP protocol behavior, including config parsing, transports, schema generation, Lua bridge behavior, doctor output, tool registry integration, or MCP tests.

## Source Of Truth

Keep [MCP support README](../../../libyaaf/mcp/README.md) aligned with implementation and tests. The README should describe what yaaf actually supports today, not every feature present in the upstream MCP specification.

Primary files to inspect:

- [Native MCP client](../../../libyaaf/mcp/mcp_client.cpp)
- [Native MCP API](../../../libyaaf/mcp/mcp_client.h)
- [Schema contract](../../../libyaaf/mcp/mcp_schema.h)
- [Generated schema factory](../../../libyaaf/mcp/mcp_schema_generated.cpp)
- [Generated schema sources](../../../libyaaf/mcp/schema)
- [Lua MCP bridge](../../../libyaaf/script/modules/script_mcp.cpp)
- [Lua tool registry](../../../lua/tools/init.lua)
- [Doctor command](../../../lua/cli/doctor.lua)
- [Plain MCP config/schema tests](../../../tests/plain/mcp_config_schema_tests.cpp)
- [Mocked MCP protocol tests](../../../tests/mock/mcp_protocol_tests.cpp)
- [Integration MCP client tests](../../../tests/integration/mcp_client_tests.cpp)
- [Real MCP fixture servers](../../../mcp-servers)

## Procedure

1. Identify the exact MCP behavior that changed: config shape, variable expansion, transport, JSON-RPC lifecycle, schema generation, tool listing, tool calling, Lua exposure, CLI behavior, or diagnostics.
2. Read [MCP support README](../../../libyaaf/mcp/README.md) and compare it against the implementation and tests listed above.
3. Update the README support sections using these categories: Configuration, Protocol Lifecycle, Tool Support, Lua And CLI Integration, Schema Support, Not Implemented Yet.
4. Keep unsupported features explicit. If a method exists only in generated schema metadata but has no client API or integration, list it as not implemented.
5. If the change is user-facing, also update the root [README](../../../README.md). If it changes architectural direction or rollout sequencing, update [MCP_PLAN](../../../MCP_PLAN.md).
6. Add or update focused MCP tests in the matching bucket: [plain config/schema tests](../../../tests/plain/mcp_config_schema_tests.cpp), [mocked protocol tests](../../../tests/mock/mcp_protocol_tests.cpp), or [integration client tests](../../../tests/integration/mcp_client_tests.cpp). Prefer the real [MCP fixture servers](../../../mcp-servers) for transport, `tools/list`, and `tools/call` regression coverage; use fake callbacks only for narrow failure modes that would be awkward to force through a real process.
7. Validate native changes with:

```powershell
cmake --build build --config Debug --target libyaaf_tests
.\build\tests\Debug\libyaaf_tests.exe --gtest_filter=Mcp*Tests.*:Mcp*IntegrationTests.*
```

8. For native behavior changes beyond documentation, also run the full test binary and covdbg coverage on `build/tests/Debug/libyaaf_tests.exe`.

## Documentation Rules

- Prefer precise support language: `supported`, `accepted`, `reported as diagnostic`, `schema metadata only`, or `not implemented`.
- Do not imply full MCP specification support when only generated metadata exists.
- Keep MCP config documented as the unchanged VS Code shape; do not add yaaf-specific keys there.
- Keep MCP path selection documented as explicit only: `--mcp <path>` or `YAAF_MCP_FILE`. Yaaf must not auto-discover `.vscode/mcp.json`.
- Mention platform limits, especially stdio being Windows-only in the current implementation.
- Keep examples short and compatible with the current implementation.
- Do not document `${input:...}` as supported; yaaf rejects it. Use `${env:NAME}` or `envFile` examples instead.
- Keep fixture servers hello-world specific. Do not copy weather/API examples into this repository's MCP test servers.
- Keep stdio fixture tests runtime-owned through `.vscode/mcp.json`; keep HTTP/SSE fixture tests prestarted through `docker-compose.mitmproxy.yml` or documented endpoint overrides instead of launching them from C++ tests.
