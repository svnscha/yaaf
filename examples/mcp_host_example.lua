-- MCP Server Example: Reverse String Tool and Greeting Prompt
--
-- This script demonstrates hosting an MCP server that exposes:
-- - A custom "reverse" tool that reverses strings
-- - A "greeting" prompt that generates a greeting message
-- - The built-in "echo" tool
--
-- To run:
--   yaaf run examples/mcp_host_example.lua
--
-- To test from an MCP client:
--   1. Create a .yaaf/mcp.json with this server as a stdio server:
--      {
--        "servers": {
--          "local": {
--            "command": "yaaf",
--            "args": ["run", "examples/mcp_host_example.lua"]
--          }
--        }
--      }
--   2. Point another MCP client (like Claude in VS Code) to this config
--   3. The client will see the tools and prompts registered below

local tool = require("tool")
local mcp = require("mcp")

-- Tool 1: Reverse String
--
-- Reverses the input text and returns metadata about the operation.
tool.register({
  spec = {
    name = "reverse",
    description = "Reverses a string",
    parameters = {
      type = "object",
      properties = {
        text = {
          type = "string",
          description = "Text to reverse"
        }
      },
      required = { "text" }
    }
  },
  execute = function(args)
    if not args or not args.text then
      return {
        tool_name = "reverse",
        content = "Error: text parameter is required",
        success = false,
        metadata = {}
      }
    end

    local text = args.text
    local reversed = string.reverse(text)

    return {
      tool_name = "reverse",
      content = reversed,
      success = true,
      metadata = {
        original_length = #text,
        reversed_length = #reversed
      }
    }
  end
})

-- Tool 2: Length Counter
--
-- Counts characters in a string.
tool.register({
  spec = {
    name = "count_chars",
    description = "Counts the number of characters in a string",
    parameters = {
      type = "object",
      properties = {
        text = {
          type = "string",
          description = "Text to count"
        }
      },
      required = { "text" }
    }
  },
  execute = function(args)
    if not args or not args.text then
      return {
        tool_name = "count_chars",
        content = "Error: text parameter is required",
        success = false,
        metadata = {}
      }
    end

    local count = #args.text

    return {
      tool_name = "count_chars",
      content = "The text contains " .. count .. " characters.",
      success = true,
      metadata = { count = count }
    }
  end
})

-- Prompt 1: Greeting
--
-- Returns a friendly greeting message. Accepts an optional "name" argument.
mcp.register_prompt({
  name = "greeting",
  description = "A friendly greeting prompt",
  arguments = {
    {
      name = "name",
      description = "Name to greet (optional)",
      required = false
    }
  },
  handler = function(args)
    local name = args and args.name or "there"
    local greeting = "Hello, " .. name .. "! How can I assist you today?"

    return {
      messages = {
        {
          role = "user",
          content = greeting
        }
      }
    }
  end
})

-- Prompt 2: System Role
--
-- Defines a system instruction for the assistant.
mcp.register_prompt({
  name = "system_role",
  description = "System role for the assistant",
  arguments = {
    {
      name = "style",
      description = "Assistant communication style: formal, casual, or technical",
      required = false
    }
  },
  handler = function(args)
    local style = args and args.style or "helpful"
    local instruction = "You are a " .. style .. " assistant. Help the user with their questions about text manipulation and string operations."

    return {
      messages = {
        {
          role = "user",
          content = instruction
        }
      }
    }
  end
})

-- Start the MCP server
--
-- Expose:
-- - reverse: custom tool to reverse strings
-- - count_chars: custom tool to count characters
-- - echo: built-in yaaf tool
-- - greeting: custom prompt
-- - system_role: custom system role prompt
--
-- The server will listen on stdin/stdout and handle MCP protocol messages
-- until the client disconnects.
mcp.host_stdio({
  tools = { "reverse", "count_chars", "echo" },
  prompts = { "greeting", "system_role" }
})
