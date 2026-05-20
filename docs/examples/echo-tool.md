# Built-In Echo Tool

The included built-in tool is `echo`. It returns the provided text unchanged, which makes it useful for checking tool wiring without depending on external data.

Run a ReAct agent with the tool:

```powershell
yaaf agent --name react --tool echo "Use the echo tool to repeat hello."
```

Call the same tool from `ask`:

```powershell
yaaf ask --tool echo "Echo hello."
```

Expected trace shape:

```text
thought: I need to echo the requested text.
tool: echo {"text":"hello"}
observation: hello
yaaf: ...final answer using the echoed result...
```

Unknown tools fail before contacting Ollama:

```powershell
yaaf agent --name react --tool unknown "test prompt"
```

Expected error shape:

```text
yaaf failed: unknown tool: unknown (available tools: echo)
```
