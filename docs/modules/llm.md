# llm

`llm` is a built-in module that exposes a provider-neutral LLM registry with registrable generate, chat, and embed callbacks.

The current implementation keeps the request and response contract aligned with the existing Ollama-backed native behavior so CLI commands and agents can migrate internally without changing their external behavior.

```lua
local llm = require("llm")
```

## Provider Registry

`llm` starts with two built-in providers:

- `ollama`: a Lua provider that talks to the Ollama HTTP API through the `http` bridge.
- `openai`: a Lua provider that talks to OpenAI-compatible `/v1/chat/completions` and `/v1/embeddings` endpoints.
- `echo`: a deterministic Lua provider for smoke tests, scripted tests, and local development without contacting a real model.

## Built-In Echo Provider

The built-in `echo` provider is useful when you want to validate request wiring without depending on Ollama, a remote endpoint, or model-specific output. It returns predictable data for all three direct LLM entry points:

- `llm.generate(...)` returns `request.prompt` as `response`.
- `llm.chat(...)` returns the last `user` message content as `message.content`.
- `llm.embed(...)` returns one-number vectors containing each input string length.

That makes it useful for testing Lua scripts, command glue, agent loops, and result-shape handling.

```lua
local llm = require("llm")

local generated = llm.generate({
	provider = "echo",
	model = "echo-model",
	prompt = "hello echo",
})

local chatted = llm.chat({
	provider = "echo",
	model = "echo-model",
	messages = {
		{ role = "system", content = "ignored" },
		{ role = "user", content = "chat echo" },
	},
})

local embedded = llm.embed({
	provider = "echo",
	model = "echo-model",
	input = { "a", "abcd" },
})

print(generated.response)           -- hello echo
print(chatted.message.content)      -- chat echo
print(embedded.embeddings[1][1])    -- 1
print(embedded.embeddings[2][1])    -- 4
```

You can register a Lua provider table and then create a client for one explicit provider name.

```lua
local llm = require("llm")

llm.register("custom", {
	generate = function(request)
		return {
			model = request.model,
			response = request.prompt,
			done = true,
		}
	end,
})

local client = llm.create("custom", { model = "custom-model" })
print(client.generate({ prompt = "hello" }).response)
```

## Functions

- `llm.register(name, provider)`: register a provider callback table.
- `llm.names()`: return the registered provider names.
- `llm.create(name, defaults)`: create a client bound to one explicit provider name.
- `llm.generate(request)`: dispatch one generate request; `request.provider` is required.
- `llm.chat(request)`: dispatch one chat request; `request.provider` is required.
- `llm.embed(request)`: dispatch one embed request; `request.provider` is required.

## OpenAI-Compatible Provider Notes

The built-in `openai` provider targets the OpenAI-compatible Chat Completions and Embeddings API shape:

- `llm.chat(...)` maps to `POST /v1/chat/completions`.
- `llm.generate(...)` is translated to one chat-completions request with a user message, plus an optional system message.
- `llm.embed(...)` maps to `POST /v1/embeddings`.
- `request.format = "json"` maps to `response_format = { type = "json_object" }`.
- A table-valued `request.format` maps to `response_format = { type = "json_schema", json_schema = { name = "yaaf_response", schema = ... } }`.
- `request.tools` maps to Chat Completions function tools, and tool-call responses are normalized back into yaaf `message.tool_calls`.
- `request.think = "low" | "medium" | "high"` is translated to `reasoning_effort` when the request has not already set that field through `request.options.extra`.
- Unsupported or provider-specific fields can be passed through `request.options.extra` for compatible servers.
- The provider reads `YAAF_OPENAI_ENDPOINT`, `YAAF_OPENAI_API_KEY`, and `YAAF_OPENAI_MODEL` from the process environment when explicit request fields are omitted.
- `llm.embed(...)` also checks `YAAF_OPENAI_EMBED_MODEL` before falling back to `YAAF_OPENAI_MODEL`.

## Notes

- `request.endpoint` defaults to the provider-specific configured endpoint when omitted.
- `request.model` must be provided explicitly or through the provider-specific OpenAI environment variables when using the built-in `openai` provider.
- `request.provider` is required for direct `llm.generate`, `llm.chat`, and `llm.embed` calls.
- `request.on_stream` may be provided for `generate` and `chat` streaming calls.
- The built-in `ollama` provider uses the Ollama HTTP API through the native `http` bridge.
- The built-in `openai` provider uses the OpenAI-compatible HTTP API through the same bridge.
- The built-in `echo` provider is local and deterministic; prefer it for smoke tests and request-shape tests where model quality does not matter.
