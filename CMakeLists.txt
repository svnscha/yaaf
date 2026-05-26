cmake_minimum_required(VERSION 3.21)

set(_local_vcpkg_toolchain "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(EXISTS "${_local_vcpkg_toolchain}")
    set(
      CMAKE_TOOLCHAIN_FILE
      "${_local_vcpkg_toolchain}"
      CACHE STRING "Vcpkg toolchain file")
  else()
    message(
      FATAL_ERROR
      "Missing vendored vcpkg checkout at ${_local_vcpkg_toolchain}. Run git submodule update --init --recursive, then bootstrap ./vcpkg before configuring.")
  endif()
endif()

project(yaaf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable strict compiler warnings
if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(Lua REQUIRED)

if(NOT WIN32)
  include(CTest)
endif()

add_subdirectory(libyaaf)
add_subdirectory(app)
add_subdirectory(tests)
