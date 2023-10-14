cmake_minimum_required( VERSION 3.7 )
project(chat_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
endif(MSVC)

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
link_directories(${OPENSSL_LIBRARY_DIR})

find_package(spdlog CONFIG REQUIRED)
INCLUDE_DIRECTORIES(${spdlog_include_dirs})

find_package(http_utils CONFIG REQUIRED)
INCLUDE_DIRECTORIES(${http_utils_include_dirs})
find_package(Threads REQUIRED)



add_executable(chat_server ${CMAKE_CURRENT_SOURCE_DIR}/chat_server.cpp ${CMAKE_CURRENT_SOURCE_DIR}/server_main.cpp)
target_link_libraries(chat_server PRIVATE chat http_utils::http_server ${CMAKE_THREAD_LIBS_INIT})
