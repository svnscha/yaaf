add_library(yaaf-process OBJECT)

if(WIN32)
    target_sources(yaaf-process PRIVATE process_win32.cpp)
else()
    target_sources(yaaf-process PRIVATE process_posix.cpp)
endif()

target_include_directories(
    yaaf-process
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(WIN32)
    target_link_libraries(yaaf-process PRIVATE ws2_32 ole32)
else()
    target_link_libraries(yaaf-process PRIVATE pthread)
endif()
