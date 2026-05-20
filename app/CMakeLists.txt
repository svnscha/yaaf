add_executable(${PROJECT_NAME}
  main.cpp
)

if(WIN32)
  target_sources(${PROJECT_NAME}
    PRIVATE
      console_utf8.win32.cpp
  )
elseif(UNIX)
  target_sources(${PROJECT_NAME}
    PRIVATE
      console_utf8.posix.cpp
  )
else()
  message(FATAL_ERROR "Unsupported platform for yaaf console setup")
endif()

target_precompile_headers(${PROJECT_NAME}
  PRIVATE
    ${CMAKE_SOURCE_DIR}/libyaaf/pch/pch_std.h
    ${CMAKE_SOURCE_DIR}/libyaaf/pch/pch_dependencies.h
)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    libyaaf
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lua"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/lua"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lua"
  COMMAND ${CMAKE_COMMAND} -E rm -rf
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/examples"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/examples"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/examples"
  COMMENT "Copying Lua runtime and examples to yaaf executable directory"
)
