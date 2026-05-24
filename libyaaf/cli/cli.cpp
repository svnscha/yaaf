#include "cli.h"

#include <CLI/CLI.hpp>

#include "../config/dotenv.h"
#include "../script/lua_runtime.h"

namespace yaaf::cli
{
namespace
{
constexpr std::string_view kDefaultOllamaEndpoint = "http://localhost:11434";
constexpr std::string_view kDefaultOllamaModel = "qwen3:0.6b";

struct ScriptCommandOptions
{
    std::string endpoint;
    std::string model = std::string(kDefaultOllamaModel);
    std::string file;
    std::filesystem::path mcp_config_path;
    std::vector<std::string> arguments;
    nlohmann::json options = nlohmann::json::object();
    nlohmann::json positionals = nlohmann::json::object();
};

struct LuaCommandOptionBinding
{
    std::string name;
    std::string type;
    std::string string_value;
    std::vector<std::string> string_values;
    bool bool_value = false;
};

struct LuaCommandPositionalBinding
{
    std::string name;
    bool multiple = false;
    std::string string_value;
    std::vector<std::string> string_values;
};

struct LuaCommand
{
    std::string name;
    std::filesystem::path file;
    nlohmann::json metadata = nlohmann::json::object();
    std::vector<LuaCommandOptionBinding> options;
    std::vector<LuaCommandPositionalBinding> positionals;
    CLI::App *app = nullptr;
};

struct GlobalOptions
{
    HttpClient::Options http;
    std::string mcp_config_path;
    std::string mcp_config_path_env;
};

struct RunCommandOptions
{
    std::string file;
    std::vector<std::string> arguments;
    std::string mcp_config_path;
    CLI::App *app = nullptr;
};

[[nodiscard]] std::filesystem::path absolute_path(const std::filesystem::path &path)
{
    return path.empty() || path.is_absolute() ? path : std::filesystem::absolute(path);
}

[[nodiscard]] std::optional<std::string> environment_or_dotenv(const yaaf::dotenv::EnvironmentFile &dotenv,
                                                               std::string_view key)
{
    const auto value = std::getenv(std::string(key).c_str());
    if (value != nullptr && !std::string_view(value).empty())
    {
        return std::string(value);
    }
    return dotenv.get(key);
}

void write_json(std::ostream &output, const nlohmann::json &payload, const bool pretty)
{
    output << (pretty ? payload.dump(2) : payload.dump()) << '\n';
}

HttpClient::Response run_get(std::string_view url, const GlobalOptions &global_options, const Services *services)
{
    if (services != nullptr && services->http_get)
    {
        return services->http_get(url, {});
    }

    HttpClient client{global_options.http};
    return client.get(url);
}

HttpClient::Response run_post(std::string_view url, std::string_view body, std::string_view content_type,
                              const GlobalOptions &global_options, const Services *services)
{
    if (services != nullptr && services->http_post)
    {
        return services->http_post(url, body, content_type, {}, nullptr);
    }

    HttpClient client{global_options.http};
    return client.post(url, body, content_type);
}

[[nodiscard]] std::string metadata_string(const nlohmann::json &metadata, std::string_view key,
                                          std::string_view fallback = {})
{
    const auto entry = metadata.find(std::string(key));
    if (entry == metadata.end() || !entry->is_string())
    {
        return std::string(fallback);
    }

    return entry->get<std::string>();
}

[[nodiscard]] bool metadata_bool(const nlohmann::json &metadata, std::string_view key, bool fallback = false)
{
    const auto entry = metadata.find(std::string(key));
    if (entry == metadata.end() || !entry->is_boolean())
    {
        return fallback;
    }

    return entry->get<bool>();
}

[[nodiscard]] std::string option_names(const nlohmann::json &metadata)
{
    std::vector<std::string> names;
    if (const auto flags = metadata.find("flags"); flags != metadata.end() && flags->is_array())
    {
        for (const auto &flag : *flags)
        {
            if (flag.is_string())
            {
                names.push_back(flag.get<std::string>());
            }
        }
    }

    if (names.empty())
    {
        const auto name = metadata_string(metadata, "name");
        if (!name.empty())
        {
            names.push_back(fmt::format("--{}", name));
        }
    }

    std::string joined;
    for (const auto &name : names)
    {
        if (!joined.empty())
        {
            joined += ',';
        }
        joined += name;
    }

    return joined;
}

[[nodiscard]] std::vector<LuaCommand> discover_lua_commands(std::string_view default_endpoint,
                                                            std::string_view default_model,
                                                            const HttpClient::Options &http_options)
{
    std::vector<LuaCommand> commands;
    const std::filesystem::path script_directory{"lua/cli"};
    if (!std::filesystem::exists(script_directory) || !std::filesystem::is_directory(script_directory))
    {
        return commands;
    }

    for (const auto &entry : std::filesystem::directory_iterator{script_directory})
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".lua")
        {
            continue;
        }

        auto name = entry.path().stem().string();
        if (name.empty() || name.front() == '-')
        {
            continue;
        }

        yaaf::script::LuaRuntimeOptions options;
        options.file_path = entry.path().string();
        options.endpoint = default_endpoint;
        options.model = default_model;
        options.http = http_options;

        commands.push_back(LuaCommand{std::move(name), entry.path(), yaaf::script::read_command_metadata(options)});
    }

    std::sort(commands.begin(), commands.end(),
              [](const LuaCommand &left, const LuaCommand &right) { return left.name < right.name; });
    return commands;
}

void register_lua_commands(CLI::App &app, std::vector<LuaCommand> &commands)
{
    for (auto &command : commands)
    {
        command.app =
            app.add_subcommand(command.name, metadata_string(command.metadata, "description",
                                                             fmt::format("Run {}", command.file.generic_string())));

        const auto options = command.metadata.find("options");
        if (options != command.metadata.end() && options->is_array())
        {
            command.options.reserve(options->size());
            for (const auto &option_metadata : *options)
            {
                auto names = option_names(option_metadata);
                if (names.empty())
                {
                    continue;
                }

                auto &binding = command.options.emplace_back();
                binding.name = metadata_string(option_metadata, "name");
                binding.type = metadata_string(option_metadata, "type", "string");
                const auto description = metadata_string(option_metadata, "description");

                if (binding.type == "flag" || binding.type == "bool")
                {
                    binding.bool_value = metadata_bool(option_metadata, "default");
                    command.app->add_flag(std::move(names), binding.bool_value, description);
                }
                else if (binding.type == "strings")
                {
                    command.app
                        ->add_option_function<std::string>(
                            std::move(names),
                            [&binding](const std::string &value) { binding.string_values.push_back(value); },
                            description)
                        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
                }
                else
                {
                    binding.string_value = metadata_string(option_metadata, "default");
                    auto *option = command.app->add_option(std::move(names), binding.string_value, description);
                    if (option_metadata.find("default") != option_metadata.end())
                    {
                        option->default_str(binding.string_value);
                    }
                    if (metadata_bool(option_metadata, "required"))
                    {
                        option->required();
                    }
                }
            }
        }

        const auto positionals = command.metadata.find("positionals");
        if (positionals != command.metadata.end() && positionals->is_array())
        {
            command.positionals.reserve(positionals->size());
            for (const auto &positional_metadata : *positionals)
            {
                const auto name = metadata_string(positional_metadata, "name");
                if (name.empty())
                {
                    continue;
                }

                auto &binding = command.positionals.emplace_back();
                binding.name = name;
                binding.multiple = metadata_bool(positional_metadata, "multiple");
                const auto description = metadata_string(positional_metadata, "description");
                CLI::Option *option = nullptr;
                if (binding.multiple)
                {
                    option = command.app->add_option(binding.name, binding.string_values, description);
                }
                else
                {
                    option = command.app->add_option(binding.name, binding.string_value, description);
                }

                if (metadata_bool(positional_metadata, "required"))
                {
                    option->required();
                }
            }
        }
    }
}

[[nodiscard]] std::filesystem::path discover_workspace_mcp_config(const std::filesystem::path &workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }
    auto candidate = workspace_root / ".yaaf" / "mcp.json";
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec) && !ec)
    {
        return candidate;
    }
    return {};
}

[[nodiscard]] std::filesystem::path resolve_mcp_config_path(const std::filesystem::path &explicit_path,
                                                            const std::filesystem::path &env_path,
                                                            const std::filesystem::path &workspace_root)
{
    if (!explicit_path.empty())
    {
        return absolute_path(explicit_path);
    }
    if (!env_path.empty())
    {
        return absolute_path(env_path);
    }
    return discover_workspace_mcp_config(workspace_root);
}

[[nodiscard]] std::filesystem::path mcp_config_path_from_options(const nlohmann::json &options,
                                                                 const GlobalOptions &global_options,
                                                                 const std::filesystem::path &workspace_root)
{
    std::filesystem::path explicit_path;
    if (const auto entry = options.find("mcp"); entry != options.end() && entry->is_string() && !entry->empty())
    {
        explicit_path = entry->get<std::string>();
    }
    if (explicit_path.empty())
    {
        explicit_path = global_options.mcp_config_path;
    }
    return resolve_mcp_config_path(explicit_path, global_options.mcp_config_path_env, workspace_root);
}

[[nodiscard]] nlohmann::json parsed_lua_options(const LuaCommand &command)
{
    nlohmann::json payload = nlohmann::json::object();
    for (const auto &option : command.options)
    {
        if (option.name.empty())
        {
            continue;
        }

        if (option.type == "flag" || option.type == "bool")
        {
            payload[option.name] = option.bool_value;
        }
        else if (option.type == "strings")
        {
            payload[option.name] = option.string_values;
        }
        else
        {
            payload[option.name] = option.string_value;
        }
    }

    return payload;
}

[[nodiscard]] nlohmann::json parsed_lua_positionals(const LuaCommand &command)
{
    nlohmann::json payload = nlohmann::json::object();
    for (const auto &positional : command.positionals)
    {
        if (positional.name.empty())
        {
            continue;
        }

        payload[positional.name] =
            positional.multiple ? nlohmann::json(positional.string_values) : nlohmann::json(positional.string_value);
    }

    return payload;
}

[[nodiscard]] LuaCommand *parsed_lua_command(std::vector<LuaCommand> &commands)
{
    for (auto &command : commands)
    {
        if (command.app != nullptr && command.app->parsed())
        {
            return &command;
        }
    }

    return nullptr;
}

[[nodiscard]] std::vector<std::string> normalize_script_arguments(std::vector<std::string> arguments)
{
    if (!arguments.empty() && arguments.front() == "--")
    {
        arguments.erase(arguments.begin());
    }

    return arguments;
}

std::vector<std::string> collect_script_arguments(const std::vector<std::string> &args, std::size_t offset)
{
    if (offset < args.size() && args[offset] == "--")
    {
        ++offset;
    }

    return {args.begin() + static_cast<std::ptrdiff_t>(offset), args.end()};
}

[[nodiscard]] std::optional<std::string> validate_explicit_script_path(std::string_view value)
{
    if (value.empty())
    {
        return std::string("run requires a Lua script path");
    }

    const std::filesystem::path path{value};
    if (path.extension() != ".lua")
    {
        return std::string("run requires a .lua script path");
    }

    return std::nullopt;
}
int run_script(const ScriptCommandOptions &script_options, const GlobalOptions &global_options, std::istream &input,
               std::ostream &output, const Services *services)
{
    yaaf::script::LuaRuntimeOptions runtime_options;
    runtime_options.file_path = script_options.file;
    runtime_options.endpoint = script_options.endpoint;
    runtime_options.model = script_options.model;
    runtime_options.arguments = script_options.arguments;
    runtime_options.options = script_options.options;
    runtime_options.positionals = script_options.positionals;
    runtime_options.workspace_root = std::filesystem::current_path();
    runtime_options.mcp_config_path = script_options.mcp_config_path;
    runtime_options.http = global_options.http;
    runtime_options.input = &input;
    runtime_options.output = &output;

    if (services == nullptr)
    {
        return yaaf::script::run_file(runtime_options);
    }

    yaaf::script::Services script_services;
    if (services->generate)
    {
        script_services.generate = [&](const yaaf::llm::GenerateRequest &request,
                                       const yaaf::llm::StreamCallback *on_stream_event) {
            return services->generate(request, on_stream_event);
        };
    }

    if (services->chat)
    {
        script_services.chat = [&](const yaaf::llm::ChatRequest &request,
                                   const yaaf::llm::ChatStreamCallback *on_stream_event) {
            return services->chat(request, on_stream_event);
        };
    }

    if (services->embed)
    {
        script_services.embed = [&](const yaaf::llm::EmbedRequest &request) { return services->embed(request); };
    }

    if (services->http_get)
    {
        script_services.http_get = [&](std::string_view url, const HttpClient::Headers &headers) {
            return services->http_get(url, headers);
        };
    }

    if (services->http_post)
    {
        script_services.http_post = [&](std::string_view url, std::string_view body, std::string_view content_type,
                                        const HttpClient::Headers &headers,
                                        const HttpClient::ResponseChunkHandler *on_response_chunk) {
            return services->http_post(url, body, content_type, headers, on_response_chunk);
        };
    }
    if (services->mcp_http_post)
    {
        script_services.mcp_http_post = [&](std::string_view url, std::string_view body, std::string_view content_type,
                                            const yaaf::mcp::Headers &headers) {
            return services->mcp_http_post(url, body, content_type, headers);
        };
    }

    return yaaf::script::run_file(runtime_options, &script_services);
}
} // namespace

int run(std::vector<std::string> args, std::istream &input, std::ostream &output, std::ostream &error_output,
        const Services *services)
{
    const auto dotenv = yaaf::dotenv::EnvironmentFile::load_from_parents();
    const auto default_endpoint =
        environment_or_dotenv(dotenv, "OLLAMA_ENDPOINT").value_or(std::string(kDefaultOllamaEndpoint));
    GlobalOptions global_options;
    global_options.http.proxy = environment_or_dotenv(dotenv, "YAAF_PROXY");
    global_options.mcp_config_path_env = environment_or_dotenv(dotenv, "YAAF_MCP_FILE").value_or("");
    global_options.http.allow_invalid_proxy_certificates =
        global_options.http.proxy.has_value() && !global_options.http.proxy->empty();

    std::reverse(args.begin(), args.end());

    CLI::App app{"Minimal yaaf CLI backed by libyaaf."};

    std::optional<std::string> get_url;
    std::optional<std::string> post_url;
    std::string request_body;
    std::string content_type = "application/json";
    bool pretty = false;
    RunCommandOptions run_command;
    auto lua_commands = discover_lua_commands(default_endpoint, kDefaultOllamaModel, global_options.http);

    auto *get_option = app.add_option("--get", get_url, "Run an HTTP GET against the given URL");
    auto *post_option = app.add_option("--post", post_url, "Run an HTTP POST against the given URL");
    get_option->excludes(post_option);
    post_option->excludes(get_option);
    app.add_option("--body", request_body, "Request body used for POST");
    app.add_option("--content-type", content_type, "Content-Type header used for POST")->default_str(content_type);
    app.add_option("--proxy", global_options.http.proxy, "Proxy URL used for all HTTP requests")
        ->default_str(global_options.http.proxy.value_or(""));
    app.add_option("--mcp", global_options.mcp_config_path, "Path to the MCP server configuration file");
    app.add_flag("--pretty", pretty, "Pretty-print the JSON output");

    run_command.app = app.add_subcommand("run", "Run a standalone Lua script");
    run_command.app->add_option("--mcp", run_command.mcp_config_path, "Path to the MCP server configuration file");
    run_command.app->add_option("file", run_command.file, "Lua script path")->required();
    run_command.app->add_option("args", run_command.arguments, "Arguments passed to the Lua script");

    register_lua_commands(app, lua_commands);

    try
    {
        app.parse(args);
    }
    catch (const CLI::ParseError &error)
    {
        return app.exit(error, output, error_output);
    }

    global_options.http.allow_invalid_proxy_certificates =
        global_options.http.proxy.has_value() && !global_options.http.proxy->empty();

    auto *lua_command = parsed_lua_command(lua_commands);
    const bool run_command_selected = run_command.app != nullptr && run_command.app->parsed();

    if (!get_url && !post_url && lua_command == nullptr && !run_command_selected)
    {
        output << app.help();
        return EXIT_SUCCESS;
    }

    try
    {
        if (run_command_selected)
        {
            if (const auto validation_error = validate_explicit_script_path(run_command.file);
                validation_error.has_value())
            {
                error_output << "yaaf failed: " << *validation_error << '\n';
                return EXIT_FAILURE;
            }

            ScriptCommandOptions script_options;
            script_options.endpoint = default_endpoint;
            script_options.model = std::string(kDefaultOllamaModel);
            script_options.file = run_command.file;
            script_options.arguments = normalize_script_arguments(run_command.arguments);
            const auto workspace_root = std::filesystem::current_path();
            const std::filesystem::path run_explicit_path =
                !run_command.mcp_config_path.empty() ? std::filesystem::path(run_command.mcp_config_path)
                                                     : std::filesystem::path(global_options.mcp_config_path);
            script_options.mcp_config_path =
                resolve_mcp_config_path(run_explicit_path, global_options.mcp_config_path_env, workspace_root);
            return run_script(script_options, global_options, input, output, services);
        }

        if (lua_command != nullptr)
        {
            ScriptCommandOptions script_options;
            script_options.endpoint = default_endpoint;
            script_options.model = std::string(kDefaultOllamaModel);
            script_options.file = lua_command->file.string();
            script_options.options = parsed_lua_options(*lua_command);
            script_options.mcp_config_path =
                mcp_config_path_from_options(script_options.options, global_options, std::filesystem::current_path());
            script_options.positionals = parsed_lua_positionals(*lua_command);
            return run_script(script_options, global_options, input, output, services);
        }

        const bool is_post = post_url.has_value();
        const auto response = is_post ? run_post(*post_url, request_body, content_type, global_options, services)
                                      : run_get(*get_url, global_options, services);

        const nlohmann::json payload = {{"method", is_post ? "POST" : "GET"},
                                        {"url", is_post ? *post_url : *get_url},
                                        {"status_code", response.status_code},
                                        {"content_type", response.content_type},
                                        {"body", response.body}};

        write_json(output, payload, pretty);
    }
    catch (const std::exception &error)
    {
        error_output << "yaaf failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int run(int argc, const char *const *argv, std::istream &input, std::ostream &output, std::ostream &error_output,
        const Services *services)
{
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));

    for (int index = 1; index < argc; ++index)
    {
        args.emplace_back(argv[index]);
    }

    return run(std::move(args), input, output, error_output, services);
}
} // namespace yaaf::cli
