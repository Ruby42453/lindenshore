cmake_minimum_required(VERSION 3.10)
project(lindenshore_blockchain_discovery CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

add_executable(discover
    src/main.cpp
    src/rpc_client.cpp
    src/log_decoder.cpp
    src/db.cpp
    src/analysis.cpp
)

target_link_libraries(discover PRIVATE CURL::libcurl SQLite::SQLite3)

# Requires GCC/Clang (__int128 is not standard C++, not supported by MSVC).
