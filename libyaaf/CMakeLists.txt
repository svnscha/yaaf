add_library(libyaaf STATIC)

file(GLOB MCP_SCHEMA_SOURCES CONFIGURE_DEPENDS
  mcp/schema/schema-*.cpp
)

set(LIBYAAF_MCP_PLATFORM_SOURCES)
if(WIN32)
  list(APPEND LIBYAAF_MCP_PLATFORM_SOURCES
    mcp/mcp_client_stdio.win32.cpp
  )
elseif(UNIX)
  list(APPEND LIBYAAF_MCP_PLATFORM_SOURCES
    mcp/mcp_client_stdio.posix.cpp
  )
else()
  message(FATAL_ERROR "Unsupported platform for MCP stdio transport")
endif()

target_sources(libyaaf
  PRIVATE
    cli/cli.cpp
    config/dotenv.cpp
    mcp/mcp_client.cpp
    ${LIBYAAF_MCP_PLATFORM_SOURCES}
    mcp/mcp_schema_generated.cpp
    ${MCP_SCHEMA_SOURCES}
    platform/executable_path.cpp
    script/lua_runtime.cpp
    script/modules/agent.cpp
    script/modules/lua_module_utils.cpp
    script/modules/script_http.cpp
    script/modules/script_yaaf.cpp
    script/modules/script_json.cpp
    script/modules/script_llm.cpp
    script/modules/script_mcp.cpp
    script/modules/tool.cpp
)

add_subdirectory(pch)
add_subdirectory(http)

target_include_directories(libyaaf
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE
    ${LUA_INCLUDE_DIR}
)

target_precompile_headers(libyaaf
  PRIVATE
    ${LIBYAAF_PCH_HEADERS}
)

target_link_libraries(libyaaf
  PUBLIC
    CURL::libcurl
    fmt::fmt
  PRIVATE
    CLI11::CLI11
    ${LUA_LIBRARIES}
    nlohmann_json::nlohmann_json
)
