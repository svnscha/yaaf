# yaaf

`yaaf` is a built-in module that exposes process, command, terminal, and runtime-default helpers.

Load it with:

```lua
local yaaf = require("yaaf")
```

## Fields

- `yaaf.args`: array of direct script arguments.
- `yaaf.options`: parsed option table for a Lua command module.
- `yaaf.positionals`: parsed positional table for a Lua command module.
- `yaaf.defaults.endpoint`: resolved Ollama endpoint.
- `yaaf.defaults.model`: command default model.

## Functions

- `yaaf.command(metadata)`: declares a Lua CLI command and returns the command wrapper used by the native CLI.
- `yaaf.read_line()`: reads one line from stdin, or returns `nil` at EOF.
- `yaaf.write(...)`: writes values to stdout after converting them with `tostring`.
- `yaaf.flush()`: flushes stdout.

## Command Metadata

Command modules return `yaaf.command({ ... })`. The CLI reads `description`, `options`, `positionals`, and `run` from that table.

Options support `name`, `flags`, `type`, `default`, `description`, and `required`. `type = "flag"` and `type = "bool"` bind booleans, `type = "strings"` accepts repeated values, and other types bind strings.

Positionals support `name`, `description`, `required`, and `multiple`.

```lua
local yaaf = require("yaaf")

return yaaf.command({
    description = "Say hello",
    positionals = {
        { name = "name", required = true, description = "Name to greet" },
    },
    run = function(command)
        print("hello " .. command.positionals.name)
    end,
})
```
