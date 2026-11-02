cmake_minimum_required(VERSION 3.15)
project(snake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(snake snake.cpp)

if(MSVC)
    target_compile_options(snake PRIVATE /utf-8)
else()
    target_compile_options(snake PRIVATE -Wall -Wextra)
endif()
