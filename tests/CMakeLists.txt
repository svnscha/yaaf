function(configure_yaaf_test_target target_name)
  target_precompile_headers(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/libyaaf/pch/pch_std.h
      ${CMAKE_SOURCE_DIR}/libyaaf/pch/pch_dependencies.h
  )

  if(MSVC)
    set_property(TARGET ${target_name} PROPERTY VS_GLOBAL_UseMultiToolTask false)
  endif()

  target_link_libraries(${target_name}
    PRIVATE
      libyaaf
      GTest::gtest
      GTest::gtest_main
      nlohmann_json::nlohmann_json
  )

  # The runtime resolves bundled Lua modules relative to the executable directory.
  # Mirror the production `yaaf` POST_BUILD copy so tests find `require("yaaf")` and
  # the bundled `lua/cli/*` command modules next to their own test binary.
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
      "$<TARGET_FILE_DIR:${target_name}>/lua"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/lua"
      "$<TARGET_FILE_DIR:${target_name}>/lua"
    COMMENT "Copying Lua runtime next to ${target_name}"
  )
endfunction()

set(libyaaf_ollama_integration_sources
  integration/ollama/ollama_client_tests.cpp
)

set(libyaaf_docker_integration_sources
  integration/docker/mcp_client_docker_tests.cpp
)

add_executable(libyaaf_tests
  integration/http_client_tests.cpp
  integration/mcp_client_tests.cpp
  integration/mcp_fixture_tests.cpp
  mock/agent_tests.cpp
  mock/cli_agent_command_tests.cpp
  mock/cli_ask_command_tests.cpp
  mock/cli_chat_command_tests.cpp
  mock/cli_embed_command_tests.cpp
  mock/cli_runtime_tests.cpp
  mock/mcp_command_tests.cpp
  mock/mcp_protocol_tests.cpp
  mock/script_lua_tests.cpp
  plain/dotenv_tests.cpp
  plain/mcp_config_schema_tests.cpp
  support/test_process_environment.cpp
)

if(WIN32)
  target_sources(libyaaf_tests
    PRIVATE
      ${libyaaf_ollama_integration_sources}
      ${libyaaf_docker_integration_sources}
  )
endif()

configure_yaaf_test_target(libyaaf_tests)

if(NOT WIN32)
  add_executable(libyaaf_ollama_integration_tests
    ${libyaaf_ollama_integration_sources}
    support/test_process_environment.cpp
  )
  configure_yaaf_test_target(libyaaf_ollama_integration_tests)

  add_executable(libyaaf_docker_integration_tests
    ${libyaaf_docker_integration_sources}
    support/test_process_environment.cpp
  )
  configure_yaaf_test_target(libyaaf_docker_integration_tests)
endif()

if(NOT WIN32)
  add_test(NAME libyaaf_tests COMMAND libyaaf_tests)
  add_test(NAME libyaaf_ollama_integration_tests COMMAND libyaaf_ollama_integration_tests)
  add_test(NAME libyaaf_docker_integration_tests COMMAND libyaaf_docker_integration_tests)

  set_tests_properties(
    libyaaf_ollama_integration_tests
    PROPERTIES
      LABELS "external;ollama"
  )
  set_tests_properties(
    libyaaf_docker_integration_tests
    PROPERTIES
      LABELS "external;docker"
  )

  set_property(TEST libyaaf_tests PROPERTY LABELS "default")
endif()
