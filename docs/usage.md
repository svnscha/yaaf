# Usage

## Build

Build requirements:

macOS:

- Xcode Command Line Tools
- CMake 3.21 or newer
- Ninja
- pkg-config
- Git
- vcpkg checked out locally with `VCPKG_ROOT` set

Linux contributor build:

- A C++20-capable toolchain such as `build-essential`
- CMake 3.21 or newer
- Ninja (`ninja-build`)
- `pkg-config`
- Git
- `zip`, `unzip`, `tar`, and `curl`
- vcpkg checked out locally with `VCPKG_ROOT` set

Recommended setup:

```sh
xcode-select --install
brew install cmake ninja
brew install pkg-config
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/vcpkg"
```

Ubuntu Linux contributor setup:

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config git zip unzip tar curl
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/vcpkg"
```

If you use VS Code with CMake Tools, either start VS Code from a shell where `VCPKG_ROOT` is already exported, or set `cmake.configureEnvironment.VCPKG_ROOT` to the same path.

Contributors on Linux can still use the normal distro-native CMake plus vcpkg path for day-to-day development builds. The release artifact path uses the musl static preset and the musl-native Linux devcontainer / CI environment.

Configure and build with CMake and the vcpkg toolchain:

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

Typical output paths are:

```text
build/app/yaaf
build/app/Debug/yaaf
```

On Linux, the normal contributor executable path is `build/app/yaaf`. The packaged Linux release artifact is `build/linux-musl-static/app/yaaf` and is built as a musl-based static executable. CI validates that artifact as statically linked and smoke-tests the packaged bundle on both Alpine and Debian.

### Linux musl release reproduction

Use this path when you want to reproduce the Linux release artifact locally.

Recommended environment:

- the repository devcontainer, which already provisions the musl-native toolchain, pinned CMake, and Ninja used by CI
- or another Alpine / musl-native environment with the same prerequisites

Configure and build the musl release artifact:

```sh
cmake --preset linux-musl-static
cmake --build build/linux-musl-static --config Release --target yaaf
```

Verify the binary is static and run the same smoke commands used by CI:

```sh
file build/linux-musl-static/app/yaaf
build/linux-musl-static/app/yaaf --help
build/linux-musl-static/app/yaaf run examples/example.lua smoke-test
```

The Linux release package step copies `lua/` and `examples/` next to the executable, so the packaged bundle remains runnable from its own directory.

The build also copies `lua/` and `examples/` next to the executable, so the app can discover its Lua command modules and run included examples from the executable directory. The native runtime resolves bundled Lua modules (such as `require("yaaf")`, `require("llm")`, and the `lua/cli/*` command modules) from this executable-adjacent `lua/` tree, so `yaaf` behaves the same regardless of the caller's current working directory. Script-local `require(...)` lookups next to the invoked `.lua` file still take precedence over the bundled tree.

Show help:

```powershell
yaaf --help
```

## Environment

At startup the CLI reads these values from the process environment first, then from the nearest parent `.env` file:

```text
YAAF_OLLAMA_ENDPOINT=http://localhost:11434
YAAF_PROXY=http://127.0.0.1:18080
YAAF_MCP_FILE=./configs/tools.mcp.json
```

`YAAF_OLLAMA_ENDPOINT` overrides the built-in `ollama` provider endpoint. `YAAF_PROXY` is used by CLI HTTP requests unless `--proxy` is passed explicitly. `YAAF_MCP_FILE` is the environment fallback for the MCP config file path; explicit `--mcp <path>` takes precedence, and when neither is set yaaf auto-discovers `.yaaf/mcp.json` in the current working directory. When a proxy is configured, the CLI relaxes TLS certificate verification for proxied requests so local mitmproxy HTTPS interception works during development.

Current defaults:

- endpoint: `http://localhost:11434`
- generate model: `qwen3:0.6b`
- react model: `ministral-3:14b`
- agent max turns: `10`

## Command Reference

All built-in commands are Lua modules under `lua/cli/`. The native executable reads each module's `yaaf.command({ ... })` metadata and exposes it as a normal CLI command.

### `ask`

`ask` sends a one-shot prompt to the configured model. Without tools it uses generate; with `--tool` it uses chat so the model can request tool calls.

```powershell
yaaf ask "How are you?"
```

Options:

| Option | Purpose |
| --- | --- |
| `--endpoint <url>` | Override the configured provider endpoint. |
| `--model <name>` | Override the model. |
| `--stream` | Stream generate output incrementally. Not supported with `--tool` or `--format json`. |
| `--think <level>` | Request model thinking output, such as `none`, `low`, `medium`, or `high`. |
| `--format <value>` | Request `json` or pass a JSON schema object. |
| `--pretty` | Pretty-print JSON output when `--format json` is used. |
| `--tool <name>` | Enable a registered tool. May be passed multiple times. |
| `--mcp <path>` | Load MCP tools from an explicit config path for this run. |

### `chat`

`chat` starts a chat session against the configured model endpoint. If a prompt is provided, it is sent as the first message; otherwise the command reads from stdin interactively.

```powershell
yaaf chat "Why is the sky blue?"
```

Options:

| Option | Purpose |
| --- | --- |
| `--endpoint <url>` | Override the configured provider endpoint. |
| `--model <name>` | Override the model. |
| `--stream` | Stream chat output incrementally. Not supported with `--tool`. |
| `--think <level>` | Request model thinking output. |
| `--tool <name>` | Enable a registered tool. May be passed multiple times. |
| `--mcp <path>` | Load MCP tools from an explicit config path for this session. |

### `agent`

`agent` runs an agent implementation from the agent registry. The registry currently contains `react`.

```powershell
yaaf agent --name react --tool echo "Use the echo tool to repeat hello."
```

Options:

| Option | Purpose |
| --- | --- |
| `--name <agent>` | Agent implementation to run. Required; currently `react`. |
| `--endpoint <url>` | Override the configured provider endpoint. |
| `--model <name>` | Override the agent model. Defaults to `ministral-3:14b`. |
| `--think <level>` | Request model thinking output. |
| `--max-turns <n>` | Limit the number of agent turns. |
| `--tool <name>` | Enable a registered tool. May be passed multiple times. |
| `--mcp <path>` | Load MCP tools from an explicit config path for this agent run. |

### `embed`

`embed` generates embeddings for one or more input texts.

```powershell
yaaf embed --model nomic-embed-text:v1.5 "hello world"
```

Options:

| Option | Purpose |
| --- | --- |
| `--endpoint <url>` | Override the configured provider endpoint. |
| `--model <name>` | Override the embedding model. |
| `--format json` | Select JSON output. `embed` only supports JSON. |
| `--dimensions <n>` | Request an embedding dimensionality override. |
| `--no-truncate` | Disable input truncation. |
| `--pretty` | Pretty-print JSON output. |

### `doctor`

`doctor` prints runtime diagnostics: Lua version, endpoint, default model, registered agents, registered tools, MCP config state, active MCP initialize status, discovered MCP tools, and which app modes support tools.

```powershell
yaaf doctor
yaaf doctor --format json --pretty
```

When MCP is configured, `doctor` actively initializes each supported server and runs tool discovery. Human-readable output shows per-server initialize status and discovered tool names; JSON output adds an `active` block under each MCP server with initialize and tool-discovery results.

Options:

| Option | Purpose |
| --- | --- |
| `--format json` | Emit the diagnostic report as JSON. |
| `--pretty` | Pretty-print JSON output. |

### `run`

`run` executes a standalone Lua file through the native script runtime.

```powershell
yaaf run ./examples/example.lua one two three
```

Options:

| Option | Purpose |
| --- | --- |
| `--mcp <path>` | Load MCP tools from an explicit config path for this script run. |
| `<file.lua>` | Path to the standalone Lua script to execute. Required. |
| `[args...]` | Positional arguments exposed to the script as `yaaf.args`. |

## Common Workflows

Basic ask:

```powershell
yaaf ask "How are you?"
```

Simple chat message:

```powershell
yaaf chat "Why is the sky blue?"
```

Stream an answer as it arrives:

```powershell
yaaf ask --stream "Write a haiku about C++."
```

Stream chat output from the chat endpoint:

```powershell
yaaf chat --stream "Reply with one short greeting."
```

Enable thinking output explicitly:

```powershell
yaaf ask --think high "What's 1+1? Only output the number."
```

Request JSON output from the model:

```powershell
yaaf ask --format json "Return a JSON object with a single field named answer containing the number 2."
```

`--format` is supported for `ask` and `embed`. Interactive `chat` does not support `--format`, and `ask --format json` must be used without `--stream`.

Use a different model:

```powershell
yaaf ask --model qwen3:0.6b "Summarize RAII in one sentence."
```

Run a standalone Lua script:

```powershell
yaaf run ./examples/example.lua one two three
```

## Embeddings

Generate one embedding:

```powershell
yaaf embed --model nomic-embed-text:v1.5 "hello world"
```

Generate embeddings for multiple inputs:

```powershell
yaaf embed --model nomic-embed-text:v1.5 "hello world" "goodbye world"
```

Disable input truncation for embeddings:

```powershell
yaaf embed --model nomic-embed-text:v1.5 --no-truncate "long input text here"
```

Pretty-print the embedding response JSON:

```powershell
yaaf --pretty embed --model nomic-embed-text:v1.5 "hello world"
```

## Proxy Testing

Start the local Docker test stack with Docker Compose:

```powershell
docker compose -f docker-compose.test-stack.yml up
```

The proxy listens on `http://127.0.0.1:18080`, the mitmweb UI is available at `http://127.0.0.1:18081`, the local `httpbin` fixture listens on `http://127.0.0.1:18082`, and the same stack starts the hello-world MCP HTTP and SSE fixtures on `http://127.0.0.1:39231/mcp` and `http://127.0.0.1:39232/mcp`.

Smoke-test the CLI proxy path with a plain HTTP request:

```powershell
yaaf --proxy http://127.0.0.1:18080 --get http://host.docker.internal:18082/get?yaaf=proxy --pretty
```

For MCP fixture traffic visible through mitmproxy, put the `host.docker.internal` fixture URL directly in the MCP config file you pass with `--mcp` or `YAAF_MCP_FILE`.

