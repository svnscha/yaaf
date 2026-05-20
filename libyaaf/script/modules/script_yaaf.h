#pragma once

struct lua_State;

namespace yaaf::script
{
struct ScriptYaafContext
{
    std::vector<std::string> arguments;
    std::string default_endpoint;
    std::string default_model;
    nlohmann::json options = nlohmann::json::object();
    nlohmann::json positionals = nlohmann::json::object();
    nlohmann::json *command_metadata = nullptr;
    std::istream *input = nullptr;
    std::ostream *output = nullptr;
};

namespace modules
{
/**
 * Registers yaaf runtime metadata as `require("yaaf")`.
 *
 * The module exposes script arguments and default runtime settings for Lua command implementations.
 */
void register_yaaf_module(lua_State *state, ScriptYaafContext &context);
} // namespace modules
} // namespace yaaf::script
