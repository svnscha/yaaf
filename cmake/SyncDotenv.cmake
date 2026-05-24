if(NOT DEFINED target_env OR target_env STREQUAL "")
  message(FATAL_ERROR "target_env is required")
endif()

if(DEFINED config AND config STREQUAL "Release")
  if(EXISTS "${target_env}")
    file(REMOVE "${target_env}")
  endif()
  return()
endif()

if(NOT DEFINED source_env OR source_env STREQUAL "")
  return()
endif()

if(EXISTS "${source_env}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source_env}" "${target_env}"
    COMMAND_ERROR_IS_FATAL ANY
  )
endif()
