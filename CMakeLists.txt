cmake_minimum_required(VERSION 3.21)

set(_local_vcpkg_toolchain "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(
      CMAKE_TOOLCHAIN_FILE
      "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Vcpkg toolchain file")
  elseif(EXISTS "${_local_vcpkg_toolchain}")
    set(
      CMAKE_TOOLCHAIN_FILE
      "${_local_vcpkg_toolchain}"
      CACHE STRING "Vcpkg toolchain file")
  endif()
endif()

project(yaaf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(Lua REQUIRED)

if(APPLE)
  include(CTest)
endif()

add_subdirectory(libyaaf)
add_subdirectory(app)
add_subdirectory(tests)
