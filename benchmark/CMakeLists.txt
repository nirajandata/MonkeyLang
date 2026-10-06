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

set(RE2C_EXECUTABLE "" CACHE FILEPATH "Path to the re2c binary")
if(NOT RE2C_EXECUTABLE)
    find_package(re2c QUIET)
    if(re2c_FOUND)
        set(RE2C_EXECUTABLE "${re2c_EXECUTABLE}")
    endif()
endif()
if(NOT RE2C_EXECUTABLE)
    find_program(RE2C_EXECUTABLE
            NAMES re2c
            HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/re2c/bin")
endif()
if(NOT RE2C_EXECUTABLE)
    message(FATAL_ERROR
            "re2c was not found. The benchmark ships a re2c baseline that needs "
            "re2c (https://re2c.org). Point -DRE2C_EXECUTABLE at a re2c binary, "
            "or add re2c to PATH.")
endif()
message(STATUS "Found re2c: ${RE2C_EXECUTABLE}")

set(monkey_re2c_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/re2c")
file(MAKE_DIRECTORY "${monkey_re2c_generated_dir}")

set(monkey_re2c_generated "${monkey_re2c_generated_dir}/monkey_re2c.c")
add_custom_command(
        OUTPUT "${monkey_re2c_generated}"
        COMMAND "${RE2C_EXECUTABLE}" --no-debug-info
        "-o${monkey_re2c_generated}"
        "${CMAKE_CURRENT_SOURCE_DIR}/re2c/monkey_lex.re"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/re2c/monkey_lex.re"
        COMMENT "Generating re2c scanner"
        VERBATIM)

add_library(monkey_re2c STATIC
        "${monkey_re2c_generated}"
        "${CMAKE_CURRENT_SOURCE_DIR}/re2c/monkey_re2c_driver.c")
target_include_directories(monkey_re2c PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(monkey_re2c PRIVATE c_std_11)
set_target_properties(monkey_re2c PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF)
target_compile_options(monkey_re2c PRIVATE -O3 -march=native)

set(monkey_flex_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/flex")
file(MAKE_DIRECTORY "${monkey_flex_generated_dir}")

set(monkey_flex_variants base avxkw cf)

set(monkey_flex_variant_sources "")
foreach(variant IN LISTS monkey_flex_variants)
    set(spec "${monkey_flex_generated_dir}/monkey_lex_${variant}.l")
    set(source "${monkey_flex_generated_dir}/monkey_lex_${variant}.c")

    if(variant STREQUAL "base")
        set(use_keyword_rules ON)
        set(flex_flags "${monkey_flex_flags}")
        set(flex_prefix_arg "")
        set(extra_defines
                MONKEY_FLEX_KEYWORD_RULES
                MONKEY_FLEX_ERROR_FLAG=monkey_flex_saw_error)
    elseif(variant STREQUAL "avxkw")
        set(use_keyword_rules OFF)
        set(flex_flags "${monkey_flex_flags}")
        set(flex_prefix_arg "-Pmonkey_flex_avxkw")
        set(extra_defines
                MONKEY_FLEX_ERROR_FLAG=monkey_flex_avxkw_saw_error)
    else()
        set(use_keyword_rules ON)
        set(flex_flags "-CF")
        set(flex_prefix_arg "-Pmonkey_flex_cf")
        set(extra_defines
                MONKEY_FLEX_KEYWORD_RULES
                MONKEY_FLEX_ERROR_FLAG=monkey_flex_cf_saw_error)
    endif()

    add_custom_command(
            OUTPUT "${source}"
            COMMAND "${CMAKE_COMMAND}"
            "-DSPEC=${CMAKE_CURRENT_SOURCE_DIR}/flex/monkey_lex.l"
            "-DRULES=${CMAKE_CURRENT_SOURCE_DIR}/flex/keyword_rules.inc"
            "-DOUTPUT=${spec}"
            "-DUSE_KEYWORD_RULES=${use_keyword_rules}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/flex/make_scanner.cmake"
            COMMAND "${FLEX_EXECUTABLE}" ${flex_prefix_arg} ${flex_flags}
            "-o${source}" "${spec}"
            DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/flex/monkey_lex.l"
            "${CMAKE_CURRENT_SOURCE_DIR}/flex/keyword_rules.inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/flex/make_scanner.cmake"
            COMMENT "Generating flex scanner: ${variant}"
            VERBATIM)

    add_library(monkey_flex_${variant} STATIC "${source}")
    target_include_directories(monkey_flex_${variant} PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_include_directories(monkey_flex_${variant} PRIVATE
            "${monkey_flex_generated_dir}")
    target_compile_features(monkey_flex_${variant} PRIVATE c_std_11)
    set_target_properties(monkey_flex_${variant} PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF)
    if(extra_defines)
        target_compile_definitions(monkey_flex_${variant} PRIVATE
                ${extra_defines})
    endif()
    target_compile_definitions(monkey_flex_${variant} PRIVATE
            _POSIX_C_SOURCE=200809L)
    target_compile_options(monkey_flex_${variant} PRIVATE
            -O3
            -march=native)

    list(APPEND monkey_flex_variant_sources "${source}")
endforeach()

add_executable(lexer_bench
        bench/bench_lexer.cpp
        flex/flex_lexer.cpp)

target_sources(lexer_bench
        PRIVATE
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        BASE_DIRS "${MONKEY_SOURCE_ROOT}/src" "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
        "${MONKEY_SOURCE_ROOT}/src/token.cppm"
        "${MONKEY_SOURCE_ROOT}/src/ascii.cppm"
        "${MONKEY_SOURCE_ROOT}/src/lexer.cppm"
        "${CMAKE_CURRENT_SOURCE_DIR}/ablate/lexer_scalar.cppm"
        "${CMAKE_CURRENT_SOURCE_DIR}/ablate/lexer_masks.cppm"
        "${CMAKE_CURRENT_SOURCE_DIR}/ablate/lexer_idents.cppm"
        "${CMAKE_CURRENT_SOURCE_DIR}/ablate/lexer_flat.cppm"
        "${CMAKE_CURRENT_SOURCE_DIR}/ablate/lexer_copy.cppm"
        "${CMAKE_CURRENT_SOURCE_DIR}/ablate/lexer_identscan.cppm"
        "${CMAKE_CURRENT_SOURCE_DIR}/ablate/lexer_kwonly.cppm"
        "${CMAKE_CURRENT_SOURCE_DIR}/ablate/lexer_flatidents.cppm")

target_compile_features(lexer_bench PRIVATE cxx_std_26)
target_compile_definitions(lexer_bench PRIVATE
        MONKEY_PINNED_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")
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
        monkey_flex_base
        monkey_flex_avxkw
        monkey_flex_cf
        monkey_re2c)

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