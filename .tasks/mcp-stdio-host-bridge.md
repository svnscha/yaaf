# Native stdio MCP host bridge for Lua scripts

## Summary

Add a native stdio MCP host bridge so `yaaf run <script.lua>` can behave as an MCP server implemented in Lua. The first version should let Lua scripts expose MCP tools and prompts through a native stdio JSON-RPC loop while reusing yaaf's generated MCP schema registry for protocol-version and method support metadata.

## Problem

Yaaf can currently consume MCP servers as a native client and expose discovered MCP tools inside the Lua runtime, but it cannot host an MCP server itself. That blocks an important workflow: writing small MCP servers directly in Lua with the same `yaaf run` experience already used for script-based tools and agents.

The repository already has:
- a native stdio MCP transport on the client side,
- a thin Lua `mcp` bridge,
- a native tool registry,
- generated MCP schema metadata with prompt and tool methods.

What is missing is the inverse bridge: a native host loop that reads stdio JSON-RPC requests, negotiates protocol version, dispatches supported server methods into Lua, and exposes a script-authored MCP server surface.

## Goal

Enable `yaaf run <script.lua>` to host an MCP stdio server from Lua, with native handling for stdio transport and JSON-RPC framing, Lua authoring for prompts and tool behavior, and generated-schema-backed support for the initial protocol surface.

## Scope

- Add a native MCP host implementation for stdio transport only.
- Extend the built-in `mcp` Lua module with server-hosting APIs for script runs.
- Support MCP initialize flow and initialized notification handling for hosted servers.
- Support `tools/list` and `tools/call` backed by yaaf's existing regular tool integration model, including built-in tools and script-registered tools selected through normal `--tool` flows.
- Support `prompts/list` and `prompts/get` backed by Lua prompt registration and handlers.
- Reuse generated schema metadata to select the advertised protocol version and known hosted methods.
- Add focused mock and integration tests for host protocol behavior and `yaaf run` server execution.
- Document the new Lua MCP server authoring flow and implementation support matrix.
- Add at least one runnable Lua example that behaves as an MCP stdio server.

## Non-Goals

- HTTP or SSE MCP server hosting.
- MCP resources, roots, sampling, elicitation, task APIs, logging control, cancellation, batching, or OAuth.
- Full generated-schema request or response JSON validation beyond protocol-version and known-method support.
- Auto-generating a standalone server manifest format outside normal Lua scripts.
- Auto-exposing unrelated remote MCP client tools as hosted tools.

## Assumptions, Dependencies, And Risks

- `yaaf run <script.lua>` remains the entry point; no separate CLI subcommand is required for v1.
- The existing built-in `mcp` module is the right user-facing home for both client and server MCP APIs.
- Hosted tool execution should reuse existing normalized tool result shapes where possible.
- Hosted tools should follow yaaf's regular tool integration model so built-in tools and script-registered tools can be selected and exposed consistently, rather than introducing a separate host-only registry concept.
- Prompt support needs both prompt descriptors and a way to produce prompt messages for `prompts/get`.
- Generated schema support today is metadata-oriented; v1 should use it for protocol and method awareness, not promise full schema validation.
- Stdio server mode must be careful about line-delimited framing, error responses, and clean process exit behavior.

## Acceptance Criteria

- [ ] A Lua script launched with `yaaf run` can enter MCP stdio server mode and successfully answer `initialize`, `tools/list`, `tools/call`, `prompts/list`, and `prompts/get`.
- [ ] Hosted tool calls can invoke built-in tools and script-authored registered tools through the existing yaaf tool integration model and return normalized MCP-compatible results.
- [ ] Hosted prompt endpoints can advertise prompt descriptors and return prompt message payloads from Lua.
- [ ] The hosted server advertises the latest supported protocol version from the generated schema registry and only claims the hosted prompt and tool method set implemented in yaaf.
- [ ] Focused mock tests cover request dispatch, method gating, protocol negotiation, and error mapping.
- [ ] Integration tests verify a real stdio-hosted Lua script can be consumed through yaaf's native MCP client path.
- [ ] User-facing docs explain how to author and run a Lua-backed MCP stdio server and clearly list the supported hosted MCP surface.

## Task Legend

- `[ ]` not started
- `[-]` in progress
- `[x]` completed
- `[!]` blocked or waiting
- `[?]` user decision required

## Tracker

| Phase | Status | Notes |
| --- | --- | --- |
| Discovery | [x] | Host API shape and tool exposure model confirmed |
| Implementation | [x] | Phase 2.1 native host loop done; Phase 2.2 Lua bridge done; Phase 2.3 wiring done |
| Validation | [x] | Mock and integration coverage passed |
| Documentation | [x] | Lua and MCP docs plus example completed |

## Phase 1 - Discovery

- [x] Finalize the hosted MCP API shape for Lua scripts.
  - [x] Define the `require("mcp")` server-facing API for starting stdio host mode from `yaaf run`.
  - [x] Define the prompt authoring shape for `prompts/list` and `prompts/get`.
  - [x] Define how hosted tool exposure maps onto the existing yaaf tool registry and normal `--tool` selection semantics.
- [x] Map the current native ownership boundaries to the new host path.
  - [x] Identify the new native host types to add under `libyaaf/mcp/` alongside the existing client code.
  - [x] Identify the Lua runtime and module changes needed in `libyaaf/script/modules/script_mcp.*` and `libyaaf/script/lua_runtime.cpp`.
  - [x] Identify the smallest integration path for client-to-host end-to-end tests using existing scripted stdio test support patterns.

## Phase 2 - Implementation

- [x] Add the native stdio MCP host transport and request loop.
  - [x] Add host-side request and response types plus stdio line-oriented JSON-RPC plumbing under `libyaaf/mcp/`.
  - [x] Implement initialize negotiation, initialized notification handling, request dispatch, and MCP error responses.
  - [x] Reuse the generated schema registry to select the hosted protocol version and gate the initial hosted method set.
- [x] Extend the Lua `mcp` bridge with server-hosting capabilities.
  - [x] Add Lua-facing host registration and start APIs in `libyaaf/script/modules/script_mcp.*`.
  - [x] Allow Lua scripts to register hosted prompt descriptors and prompt handlers.
  - [x] Allow host mode to expose selected yaaf tools through MCP `tools/list` and `tools/call`.
- [x] Wire hosted tool and prompt execution into existing yaaf runtime facilities.
  - [x] Adapt yaaf tool specs and execution results into MCP server tool descriptors and call results.
  - [x] Define prompt descriptor fields and message payload mapping for `prompts/get`.
  - [x] Ensure hosted server mode does not accidentally expose client-configured remote MCP tools as hosted tools.

## Phase 3 - Validation

- [x] Add focused native tests for the hosted MCP protocol path.
  - [x] Extend `tests/mock/mcp_protocol_tests.cpp` with host-side negotiation, dispatch, and error-shape coverage.
  - [x] Extend schema-support tests only where hosted method visibility or registry usage changes are observable.
  - [x] Add cases for unsupported methods, malformed params, and prompt or tool result mapping.
- [x] Add end-to-end stdio runtime coverage for Lua-hosted MCP servers.
  - [x] Add an integration test where a Lua script hosts an MCP stdio server through `yaaf run`.
  - [x] Add a consuming-client integration test that connects to the hosted Lua server and exercises both prompts and tools.
  - [x] Run the smallest relevant test and build loop for touched MCP and script-runtime targets.

## Phase 4 - Documentation

- [x] Document Lua-backed MCP server authoring and hosting.
  - [x] Update `libyaaf/mcp/README.md` to describe both consuming and hosting support plus the new support matrix.
  - [x] Update `docs/modules/mcp.md` and `docs/lua.md` with the hosted API and `yaaf run` workflow.
  - [x] Add or update an example Lua script that exposes MCP tools and prompts over stdio.
