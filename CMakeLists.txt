cmake_minimum_required(VERSION 3.20.0)
project(OverKey VERSION 0.0.0)

set(CMAKE_CXX_STANDARD 23)

include_directories(./include)

file(GLOB_RECURSE SRC_FILES ./src/*.cpp)

add_executable(${PROJECT_NAME} ${SRC_FILES})

install(TARGETS ${PROJECT_NAME})
