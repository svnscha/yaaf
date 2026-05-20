# llm

`llm` is a built-in module that exposes a provider-neutral LLM registry with registrable generate, chat, and embed callbacks.

The current implementation keeps the request and response contract aligned with the existing Ollama-backed native behavior so CLI commands and agents can migrate internally without changing their external behavior.

```lua
local llm = require("llm")
```

## Provider Registry

`llm` starts with two built-in providers:

- `ollama`: a Lua provider that talks to the Ollama HTTP API through the `http` bridge.
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

## Notes

- `request.endpoint` and `request.model` default to the runtime defaults when omitted.
- `request.provider` is required for direct `llm.generate`, `llm.chat`, and `llm.embed` calls.
- `request.on_stream` may be provided for `generate` and `chat` streaming calls.
- The built-in `ollama` provider uses the Ollama HTTP API through the native `http` bridge.
- The built-in `echo` provider is local and deterministic; prefer it for smoke tests and request-shape tests where model quality does not matter.
