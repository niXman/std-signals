cmake_minimum_required(VERSION 2.8)
project(std_signals)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -fsanitize=address")

add_definitions(
    -UNDEBUG
)

add_executable(
    ${PROJECT_NAME}
    signals.hpp
    test.cpp
)
