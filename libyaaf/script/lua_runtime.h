#pragma once

#include "modules/agent.h"
#include "modules/script_http.h"
#include "modules/script_json.h"
#include "modules/script_llm.h"
#include "modules/script_mcp.h"
#include "modules/script_yaaf.h"
#include "modules/tool.h"

namespace yaaf::script
{
struct LuaRuntimeOptions
{
    std::string file_path;
    std::string endpoint;
    std::string model;
    std::vector<std::string> arguments;
    nlohmann::json options = nlohmann::json::object();
    nlohmann::json positionals = nlohmann::json::object();
    std::filesystem::path workspace_root;
    /**
     * Optional override for the directory that contains the bundled `lua/` runtime tree.
     *
     * When empty, the runtime resolves bundled modules relative to the executable
     * directory (`yaaf::platform::executable_directory()`). Tests and embedders that
     * point at a different layout can set this explicitly.
     */
    std::filesystem::path runtime_root;
    std::filesystem::path mcp_config_path;
    HttpClient::Options http;
    std::istream *input = nullptr;
    std::ostream *output = nullptr;
};

/**
 * Loads command metadata declared by a Lua script without running command behavior.
 *
 * Scripts declare metadata by returning `yaaf.command({ ..., run = function(command) ... end })`.
 *
 * @param options Runtime defaults and target script path.
 * @return Metadata table declared by the script as JSON.
 * @throws std::runtime_error When the script fails or does not declare command metadata.
 */
[[nodiscard]] nlohmann::json read_command_metadata(const LuaRuntimeOptions &options);

/**
 * Executes a Lua script with yaaf runtime modules registered.
 *
 * The script can load runtime arguments with `require("yaaf")`, JSON helpers with `require("json")`,
 * the native HTTP bridge with `require("http")`, and the provider-neutral LLM bridge with `require("llm")`.
 *
 * @param options Runtime defaults and target script path.
 * @param services Optional service overrides used by tests.
 * @return Process-style exit code.
 * @throws std::invalid_argument When required options are missing.
 * @throws std::runtime_error When Lua cannot be initialized or the script fails.
 */
[[nodiscard]] int run_file(const LuaRuntimeOptions &options, const Services *services = nullptr);
} // namespace yaaf::script
