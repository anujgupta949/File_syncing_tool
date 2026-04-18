cmake_minimum_required(VERSION 3.16)
project(lan_filesync LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(lan_filesync
    main.cpp
    FileSearch.cpp
    compare.cpp
    server.cpp
    cJSON.c
)

target_include_directories(lan_filesync PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lan_filesync PRIVATE ws2_32 shlwapi)
