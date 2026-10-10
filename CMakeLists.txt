cmake_minimum_required(VERSION 4.2)
project(Monkey)

set(CMAKE_CXX_STANDARD 26)

add_executable(Monkey
        src/main.cpp
)

target_sources(Monkey PRIVATE
        FILE_SET CXX_MODULES FILES
        src/token.cppm
        src/ascii.cppm
        src/ast.cppm
        src/lexer.cppm
        src/parser.cppm
        src/semantic.cppm
        src/nir.cppm
        src/codegen.cppm
)

target_compile_options(Monkey PRIVATE -march=native -O3 -freflection)

option(MONKEY_BUILD_BENCHMARKS "Build the lexer benchmarks (needs flex)" OFF)
if(MONKEY_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()