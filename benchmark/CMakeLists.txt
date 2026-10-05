cmake_minimum_required(VERSION 4.2)

project(MonkeyLexerBench LANGUAGES CXX C)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    include(CTest)
endif()

if(NOT CMAKE_GENERATOR MATCHES "Ninja|Visual Studio")
    message(FATAL_ERROR
            "The lexer benchmarks build C++26 modules, which CMake only supports "
            "with the Ninja generator. Configure with -G Ninja.")
endif()

find_package(FLEX QUIET)
if(NOT FLEX_FOUND)
    message(FATAL_ERROR
            "flex was not found. Install it (Debian/Ubuntu: apt install flex, "
            "Fedora: dnf install flex, macOS: brew install flex), or point "
            "-DFLEX_EXECUTABLE at it.")
endif()

include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
        SYSTEM
        EXCLUDE_FROM_ALL
        FIND_PACKAGE_ARGS NAMES benchmark
)
FetchContent_MakeAvailable(googlebenchmark)

cmake_path(ABSOLUTE_PATH CMAKE_CURRENT_SOURCE_DIR
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE monkey_this_dir)
cmake_path(APPEND monkey_this_dir ".." OUTPUT_VARIABLE MONKEY_SOURCE_ROOT)
cmake_path(NORMAL_PATH MONKEY_SOURCE_ROOT)

option(MONKEY_FLEX_FULL
        "Build the flex scanner without input equivalence classes" ON)

option(MONKEY_FLEX_KEYWORD_RULES "Recognise keywords with flex rules" ON)

set(monkey_flex_flags "")
if(MONKEY_FLEX_FULL)
    string(APPEND monkey_flex_flags " --full")
endif()
string(STRIP "${monkey_flex_flags}" monkey_flex_flags)

set(monkey_flex_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/flex")
file(MAKE_DIRECTORY "${monkey_flex_generated_dir}")

FLEX_TARGET(monkey_scanner
        "${CMAKE_CURRENT_SOURCE_DIR}/flex/monkey_lex.l"
        "${monkey_flex_generated_dir}/monkey_lex.c"
        COMPILE_FLAGS "${monkey_flex_flags}")

add_library(monkey_flex_scanner STATIC ${FLEX_monkey_scanner_OUTPUTS})
target_compile_features(monkey_flex_scanner PRIVATE c_std_11)
set_target_properties(monkey_flex_scanner PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF)
target_include_directories(monkey_flex_scanner PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(monkey_flex_scanner PRIVATE
        -O3
        $<$<NOT:$<BOOL:${MONKEY_FLEX_KEYWORD_RULES}>>:-march=native>)
target_compile_definitions(monkey_flex_scanner PRIVATE
        $<$<BOOL:${MONKEY_FLEX_KEYWORD_RULES}>:MONKEY_FLEX_KEYWORD_RULES>
        _POSIX_C_SOURCE=200809L)

add_executable(lexer_bench
        bench/bench_lexer.cpp
        flex/flex_lexer.cpp)

target_sources(lexer_bench
        PRIVATE
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        BASE_DIRS "${MONKEY_SOURCE_ROOT}/src"
        FILES
        "${MONKEY_SOURCE_ROOT}/src/token.cppm"
        "${MONKEY_SOURCE_ROOT}/src/ascii.cppm"
        "${MONKEY_SOURCE_ROOT}/src/lexer.cppm")

target_compile_features(lexer_bench PRIVATE cxx_std_26)
set_target_properties(lexer_bench PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

target_include_directories(lexer_bench PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_options(lexer_bench PRIVATE -O3 -march=native)

target_compile_definitions(lexer_bench PRIVATE
        MONKEY_BENCH_CORPUS_DIR="${CMAKE_CURRENT_BINARY_DIR}/corpus"
        MONKEY_BENCH_SRC_DIR="${MONKEY_SOURCE_ROOT}/src")

target_link_libraries(lexer_bench PRIVATE
        benchmark::benchmark
        monkey_flex_scanner)

if(BUILD_TESTING)
    add_test(NAME lexer_bench_verify
            COMMAND lexer_bench --verify-only --quiet
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(lexer_bench_verify PROPERTIES TIMEOUT 300)
endif()

add_custom_target(bench
        COMMAND lexer_bench "--corpus-dir=${CMAKE_CURRENT_BINARY_DIR}/corpus"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        USES_TERMINAL
        VERBATIM
        COMMENT "Comparing the AVX-512 lexer against the flex reference scanner")