cmake_minimum_required(VERSION 4.2)
project(Monkey)

set(CMAKE_CXX_STANDARD 26)

add_executable(Monkey
        src/main.cpp
        src/lexer.hpp)
