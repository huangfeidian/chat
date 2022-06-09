cmake_minimum_required( VERSION 3.7 )
project(chat)

set(CMAKE_BUILD_TYPE Debug) 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
endif(MSVC)

find_package(nlohmann_json CONFIG REQUIRED)

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/include)

FILE(GLOB_RECURSE SRC_LISTS ${CMAKE_SOURCE_DIR}/src/*.cpp)
FILE(GLOB_RECURSE HEADER_LISTS ${CMAKE_SOURCE_DIR}/include/*.h)

add_library(${PROJECT_NAME} ${SRC_LISTS} ${HEADER_LISTS})
target_link_libraries(${PROJECT_NAME} nlohmann_json::nlohmann_json)
add_subdirectory(./test)