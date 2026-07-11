cmake_minimum_required(VERSION 4.2)
project(Monkey)

set(CMAKE_CXX_STANDARD 26)

add_executable(Monkey
        src/main.cpp
        src/lexer.hpp
        src/token.hpp
        src/ascii.hpp
        src/ast.hpp
        src/parser.hpp)

target_compile_options(Monkey PRIVATE -march=native -O3)

